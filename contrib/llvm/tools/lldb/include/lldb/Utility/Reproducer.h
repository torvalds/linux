//===-- Reproducer.h --------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UTILITY_REPRODUCER_H
#define LLDB_UTILITY_REPRODUCER_H

#include "lldb/Utility/FileSpec.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/YAMLTraits.h"

#include <mutex>
#include <string>
#include <vector>

namespace lldb_private {
namespace repro {

class Reproducer;

enum class ReproducerMode {
  Capture,
  Replay,
  Off,
};

/// Abstraction for information associated with a provider. This information
/// is serialized into an index which is used by the loader.
struct ProviderInfo {
  std::string name;
  std::vector<std::string> files;
};

/// The provider defines an interface for generating files needed for
/// reproducing. The provider must populate its ProviderInfo to communicate
/// its name and files to the index, before registering with the generator,
/// i.e. in the constructor.
///
/// Different components will implement different providers.
class ProviderBase {
public:
  virtual ~ProviderBase() = default;

  const ProviderInfo &GetInfo() const { return m_info; }
  const FileSpec &GetRoot() const { return m_root; }

  /// The Keep method is called when it is decided that we need to keep the
  /// data in order to provide a reproducer.
  virtual void Keep(){};

  /// The Discard method is called when it is decided that we do not need to
  /// keep any information and will not generate a reproducer.
  virtual void Discard(){};

  // Returns the class ID for this type.
  static const void *ClassID() { return &ID; }

  // Returns the class ID for the dynamic type of this Provider instance.
  virtual const void *DynamicClassID() const = 0;

protected:
  ProviderBase(const FileSpec &root) : m_root(root) {}

  /// Every provider keeps track of its own files.
  ProviderInfo m_info;
private:
  /// Every provider knows where to dump its potential files.
  FileSpec m_root;

  virtual void anchor();
  static char ID;
};

template <typename ThisProviderT> class Provider : public ProviderBase {
public:
  static const void *ClassID() { return &ThisProviderT::ID; }

  const void *DynamicClassID() const override { return &ThisProviderT::ID; }

protected:
  using ProviderBase::ProviderBase; // Inherit constructor.
};

/// The generator is responsible for the logic needed to generate a
/// reproducer. For doing so it relies on providers, who serialize data that
/// is necessary for reproducing  a failure.
class Generator final {
public:
  Generator(const FileSpec &root);
  ~Generator();

  /// Method to indicate we want to keep the reproducer. If reproducer
  /// generation is disabled, this does nothing.
  void Keep();

  /// Method to indicate we do not want to keep the reproducer. This is
  /// unaffected by whether or not generation reproduction is enabled, as we
  /// might need to clean up files already written to disk.
  void Discard();

  /// Create and register a new provider.
  template <typename T> T *Create() {
    std::unique_ptr<ProviderBase> provider = llvm::make_unique<T>(m_root);
    return static_cast<T *>(Register(std::move(provider)));
  }

  /// Get an existing provider.
  template <typename T> T *Get() {
    auto it = m_providers.find(T::ClassID());
    if (it == m_providers.end())
      return nullptr;
    return static_cast<T *>(it->second.get());
  }

  /// Get a provider if it exists, otherwise create it.
  template <typename T> T &GetOrCreate() {
    auto *provider = Get<T>();
    if (provider)
      return *provider;
    return *Create<T>();
  }

  const FileSpec &GetRoot() const;

private:
  friend Reproducer;

  ProviderBase *Register(std::unique_ptr<ProviderBase> provider);

  /// Builds and index with provider info.
  void AddProvidersToIndex();

  /// Map of provider IDs to provider instances.
  llvm::DenseMap<const void *, std::unique_ptr<ProviderBase>> m_providers;
  std::mutex m_providers_mutex;

  /// The reproducer root directory.
  FileSpec m_root;

  /// Flag to ensure that we never call both keep and discard.
  bool m_done;
};

class Loader final {
public:
  Loader(const FileSpec &root);

  llvm::Optional<ProviderInfo> GetProviderInfo(llvm::StringRef name);
  llvm::Error LoadIndex();

  const FileSpec &GetRoot() const { return m_root; }

private:
  llvm::StringMap<ProviderInfo> m_provider_info;
  FileSpec m_root;
  bool m_loaded;
};

/// The reproducer enables clients to obtain access to the Generator and
/// Loader.
class Reproducer {
public:
  static Reproducer &Instance();
  static llvm::Error Initialize(ReproducerMode mode,
                                llvm::Optional<FileSpec> root);
  static void Terminate();

  Reproducer() = default;

  Generator *GetGenerator();
  Loader *GetLoader();

  const Generator *GetGenerator() const;
  const Loader *GetLoader() const;

  FileSpec GetReproducerPath() const;

protected:
  llvm::Error SetCapture(llvm::Optional<FileSpec> root);
  llvm::Error SetReplay(llvm::Optional<FileSpec> root);

private:
  static llvm::Optional<Reproducer> &InstanceImpl();

  llvm::Optional<Generator> m_generator;
  llvm::Optional<Loader> m_loader;

  mutable std::mutex m_mutex;
};

} // namespace repro
} // namespace lldb_private

LLVM_YAML_IS_DOCUMENT_LIST_VECTOR(lldb_private::repro::ProviderInfo)

namespace llvm {
namespace yaml {

template <> struct MappingTraits<lldb_private::repro::ProviderInfo> {
  static void mapping(IO &io, lldb_private::repro::ProviderInfo &info) {
    io.mapRequired("name", info.name);
    io.mapOptional("files", info.files);
  }
};
} // namespace yaml
} // namespace llvm

#endif // LLDB_UTILITY_REPRODUCER_H
