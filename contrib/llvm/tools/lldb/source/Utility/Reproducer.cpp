//===-- Reproducer.cpp ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/Reproducer.h"
#include "lldb/Utility/LLDBAssert.h"

#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Threading.h"
#include "llvm/Support/raw_ostream.h"

using namespace lldb_private;
using namespace lldb_private::repro;
using namespace llvm;
using namespace llvm::yaml;

Reproducer &Reproducer::Instance() { return *InstanceImpl(); }

llvm::Error Reproducer::Initialize(ReproducerMode mode,
                                   llvm::Optional<FileSpec> root) {
  lldbassert(!InstanceImpl() && "Already initialized.");
  InstanceImpl().emplace();

  switch (mode) {
  case ReproducerMode::Capture: {
    if (!root) {
      SmallString<128> repro_dir;
      auto ec = sys::fs::createUniqueDirectory("reproducer", repro_dir);
      if (ec)
        return make_error<StringError>(
            "unable to create unique reproducer directory", ec);
      root.emplace(repro_dir);
    } else {
      auto ec = sys::fs::create_directory(root->GetPath());
      if (ec)
        return make_error<StringError>("unable to create reproducer directory",
                                       ec);
    }
    return Instance().SetCapture(root);
  } break;
  case ReproducerMode::Replay:
    return Instance().SetReplay(root);
  case ReproducerMode::Off:
    break;
  };

  return Error::success();
}

void Reproducer::Terminate() {
  lldbassert(InstanceImpl() && "Already terminated.");
  InstanceImpl().reset();
}

Optional<Reproducer> &Reproducer::InstanceImpl() {
  static Optional<Reproducer> g_reproducer;
  return g_reproducer;
}

const Generator *Reproducer::GetGenerator() const {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_generator)
    return &(*m_generator);
  return nullptr;
}

const Loader *Reproducer::GetLoader() const {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_loader)
    return &(*m_loader);
  return nullptr;
}

Generator *Reproducer::GetGenerator() {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_generator)
    return &(*m_generator);
  return nullptr;
}

Loader *Reproducer::GetLoader() {
  std::lock_guard<std::mutex> guard(m_mutex);
  if (m_loader)
    return &(*m_loader);
  return nullptr;
}

llvm::Error Reproducer::SetCapture(llvm::Optional<FileSpec> root) {
  std::lock_guard<std::mutex> guard(m_mutex);

  if (root && m_loader)
    return make_error<StringError>(
        "cannot generate a reproducer when replay one",
        inconvertibleErrorCode());

  if (!root) {
    m_generator.reset();
    return Error::success();
  }

  m_generator.emplace(*root);
  return Error::success();
}

llvm::Error Reproducer::SetReplay(llvm::Optional<FileSpec> root) {
  std::lock_guard<std::mutex> guard(m_mutex);

  if (root && m_generator)
    return make_error<StringError>(
        "cannot replay a reproducer when generating one",
        inconvertibleErrorCode());

  if (!root) {
    m_loader.reset();
    return Error::success();
  }

  m_loader.emplace(*root);
  if (auto e = m_loader->LoadIndex())
    return e;

  return Error::success();
}

FileSpec Reproducer::GetReproducerPath() const {
  if (auto g = GetGenerator())
    return g->GetRoot();
  if (auto l = GetLoader())
    return l->GetRoot();
  return {};
}

Generator::Generator(const FileSpec &root) : m_root(root), m_done(false) {}

Generator::~Generator() {}

ProviderBase *Generator::Register(std::unique_ptr<ProviderBase> provider) {
  std::lock_guard<std::mutex> lock(m_providers_mutex);
  std::pair<const void *, std::unique_ptr<ProviderBase>> key_value(
      provider->DynamicClassID(), std::move(provider));
  auto e = m_providers.insert(std::move(key_value));
  return e.first->getSecond().get();
}

void Generator::Keep() {
  assert(!m_done);
  m_done = true;

  for (auto &provider : m_providers)
    provider.second->Keep();

  AddProvidersToIndex();
}

void Generator::Discard() {
  assert(!m_done);
  m_done = true;

  for (auto &provider : m_providers)
    provider.second->Discard();

  llvm::sys::fs::remove_directories(m_root.GetPath());
}

const FileSpec &Generator::GetRoot() const { return m_root; }

void Generator::AddProvidersToIndex() {
  FileSpec index = m_root;
  index.AppendPathComponent("index.yaml");

  std::error_code EC;
  auto strm = llvm::make_unique<raw_fd_ostream>(index.GetPath(), EC,
                                                sys::fs::OpenFlags::F_None);
  yaml::Output yout(*strm);

  for (auto &provider : m_providers) {
    auto &provider_info = provider.second->GetInfo();
    yout << const_cast<ProviderInfo &>(provider_info);
  }
}

Loader::Loader(const FileSpec &root) : m_root(root), m_loaded(false) {}

llvm::Error Loader::LoadIndex() {
  if (m_loaded)
    return llvm::Error::success();

  FileSpec index = m_root.CopyByAppendingPathComponent("index.yaml");

  auto error_or_file = MemoryBuffer::getFile(index.GetPath());
  if (auto err = error_or_file.getError())
    return make_error<StringError>("unable to load reproducer index", err);

  std::vector<ProviderInfo> provider_info;
  yaml::Input yin((*error_or_file)->getBuffer());
  yin >> provider_info;

  if (auto err = yin.error())
    return make_error<StringError>("unable to read reproducer index", err);

  for (auto &info : provider_info)
    m_provider_info[info.name] = info;

  m_loaded = true;

  return llvm::Error::success();
}

llvm::Optional<ProviderInfo> Loader::GetProviderInfo(StringRef name) {
  assert(m_loaded);

  auto it = m_provider_info.find(name);
  if (it == m_provider_info.end())
    return llvm::None;

  return it->second;
}

void ProviderBase::anchor() {}
char ProviderBase::ID = 0;
