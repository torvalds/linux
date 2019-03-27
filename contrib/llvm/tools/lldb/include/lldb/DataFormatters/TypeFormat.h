//===-- TypeFormat.h ----------------------------------------------*- C++
//-*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_TypeFormat_h_
#define lldb_TypeFormat_h_


#include <functional>
#include <string>
#include <unordered_map>


#include "lldb/lldb-enumerations.h"
#include "lldb/lldb-public.h"

#include "lldb/Core/ValueObject.h"

namespace lldb_private {
class TypeFormatImpl {
public:
  class Flags {
  public:
    Flags() : m_flags(lldb::eTypeOptionCascade) {}

    Flags(const Flags &other) : m_flags(other.m_flags) {}

    Flags(uint32_t value) : m_flags(value) {}

    Flags &operator=(const Flags &rhs) {
      if (&rhs != this)
        m_flags = rhs.m_flags;

      return *this;
    }

    Flags &operator=(const uint32_t &rhs) {
      m_flags = rhs;
      return *this;
    }

    Flags &Clear() {
      m_flags = 0;
      return *this;
    }

    bool GetCascades() const {
      return (m_flags & lldb::eTypeOptionCascade) == lldb::eTypeOptionCascade;
    }

    Flags &SetCascades(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionCascade;
      else
        m_flags &= ~lldb::eTypeOptionCascade;
      return *this;
    }

    bool GetSkipPointers() const {
      return (m_flags & lldb::eTypeOptionSkipPointers) ==
             lldb::eTypeOptionSkipPointers;
    }

    Flags &SetSkipPointers(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionSkipPointers;
      else
        m_flags &= ~lldb::eTypeOptionSkipPointers;
      return *this;
    }

    bool GetSkipReferences() const {
      return (m_flags & lldb::eTypeOptionSkipReferences) ==
             lldb::eTypeOptionSkipReferences;
    }

    Flags &SetSkipReferences(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionSkipReferences;
      else
        m_flags &= ~lldb::eTypeOptionSkipReferences;
      return *this;
    }

    bool GetNonCacheable() const {
      return (m_flags & lldb::eTypeOptionNonCacheable) ==
             lldb::eTypeOptionNonCacheable;
    }

    Flags &SetNonCacheable(bool value = true) {
      if (value)
        m_flags |= lldb::eTypeOptionNonCacheable;
      else
        m_flags &= ~lldb::eTypeOptionNonCacheable;
      return *this;
    }

    uint32_t GetValue() { return m_flags; }

    void SetValue(uint32_t value) { m_flags = value; }

  private:
    uint32_t m_flags;
  };

  TypeFormatImpl(const Flags &flags = Flags());

  typedef std::shared_ptr<TypeFormatImpl> SharedPointer;

  virtual ~TypeFormatImpl();

  bool Cascades() const { return m_flags.GetCascades(); }

  bool SkipsPointers() const { return m_flags.GetSkipPointers(); }

  bool SkipsReferences() const { return m_flags.GetSkipReferences(); }

  bool NonCacheable() const { return m_flags.GetNonCacheable(); }

  void SetCascades(bool value) { m_flags.SetCascades(value); }

  void SetSkipsPointers(bool value) { m_flags.SetSkipPointers(value); }

  void SetSkipsReferences(bool value) { m_flags.SetSkipReferences(value); }

  void SetNonCacheable(bool value) { m_flags.SetNonCacheable(value); }

  uint32_t GetOptions() { return m_flags.GetValue(); }

  void SetOptions(uint32_t value) { m_flags.SetValue(value); }

  uint32_t &GetRevision() { return m_my_revision; }

  enum class Type { eTypeUnknown, eTypeFormat, eTypeEnum };

  virtual Type GetType() { return Type::eTypeUnknown; }

  // we are using a ValueObject* instead of a ValueObjectSP because we do not
  // need to hold on to this for extended periods of time and we trust the
  // ValueObject to stay around for as long as it is required for us to
  // generate its value
  virtual bool FormatObject(ValueObject *valobj, std::string &dest) const = 0;

  virtual std::string GetDescription() = 0;

protected:
  Flags m_flags;
  uint32_t m_my_revision;

private:
  DISALLOW_COPY_AND_ASSIGN(TypeFormatImpl);
};

class TypeFormatImpl_Format : public TypeFormatImpl {
public:
  TypeFormatImpl_Format(lldb::Format f = lldb::eFormatInvalid,
                        const TypeFormatImpl::Flags &flags = Flags());

  typedef std::shared_ptr<TypeFormatImpl_Format> SharedPointer;

  ~TypeFormatImpl_Format() override;

  lldb::Format GetFormat() const { return m_format; }

  void SetFormat(lldb::Format fmt) { m_format = fmt; }

  TypeFormatImpl::Type GetType() override {
    return TypeFormatImpl::Type::eTypeFormat;
  }

  bool FormatObject(ValueObject *valobj, std::string &dest) const override;

  std::string GetDescription() override;

protected:
  lldb::Format m_format;

private:
  DISALLOW_COPY_AND_ASSIGN(TypeFormatImpl_Format);
};

class TypeFormatImpl_EnumType : public TypeFormatImpl {
public:
  TypeFormatImpl_EnumType(ConstString type_name = ConstString(""),
                          const TypeFormatImpl::Flags &flags = Flags());

  typedef std::shared_ptr<TypeFormatImpl_EnumType> SharedPointer;

  ~TypeFormatImpl_EnumType() override;

  ConstString GetTypeName() { return m_enum_type; }

  void SetTypeName(ConstString enum_type) { m_enum_type = enum_type; }

  TypeFormatImpl::Type GetType() override {
    return TypeFormatImpl::Type::eTypeEnum;
  }

  bool FormatObject(ValueObject *valobj, std::string &dest) const override;

  std::string GetDescription() override;

protected:
  ConstString m_enum_type;
  mutable std::unordered_map<void *, CompilerType> m_types;

private:
  DISALLOW_COPY_AND_ASSIGN(TypeFormatImpl_EnumType);
};
} // namespace lldb_private

#endif // lldb_TypeFormat_h_
