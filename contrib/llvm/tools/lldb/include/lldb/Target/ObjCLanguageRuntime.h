//===-- ObjCLanguageRuntime.h -----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ObjCLanguageRuntime_h_
#define liblldb_ObjCLanguageRuntime_h_

#include <functional>
#include <map>
#include <memory>
#include <unordered_set>

#include "llvm/Support/Casting.h"

#include "lldb/Core/PluginInterface.h"
#include "lldb/Core/ThreadSafeDenseMap.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/DeclVendor.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/LanguageRuntime.h"
#include "lldb/lldb-private.h"

class CommandObjectObjC_ClassTable_Dump;

namespace lldb_private {

class UtilityFunction;

class ObjCLanguageRuntime : public LanguageRuntime {
public:
  enum class ObjCRuntimeVersions {
    eObjC_VersionUnknown = 0,
    eAppleObjC_V1 = 1,
    eAppleObjC_V2 = 2
  };

  typedef lldb::addr_t ObjCISA;

  class ClassDescriptor;
  typedef std::shared_ptr<ClassDescriptor> ClassDescriptorSP;

  // the information that we want to support retrieving from an ObjC class this
  // needs to be pure virtual since there are at least 2 different
  // implementations of the runtime, and more might come
  class ClassDescriptor {
  public:
    ClassDescriptor()
        : m_is_kvo(eLazyBoolCalculate), m_is_cf(eLazyBoolCalculate),
          m_type_wp() {}

    virtual ~ClassDescriptor() = default;

    virtual ConstString GetClassName() = 0;

    virtual ClassDescriptorSP GetSuperclass() = 0;

    virtual ClassDescriptorSP GetMetaclass() const = 0;

    // virtual if any implementation has some other version-specific rules but
    // for the known v1/v2 this is all that needs to be done
    virtual bool IsKVO() {
      if (m_is_kvo == eLazyBoolCalculate) {
        const char *class_name = GetClassName().AsCString();
        if (class_name && *class_name)
          m_is_kvo =
              (LazyBool)(strstr(class_name, "NSKVONotifying_") == class_name);
      }
      return (m_is_kvo == eLazyBoolYes);
    }

    // virtual if any implementation has some other version-specific rules but
    // for the known v1/v2 this is all that needs to be done
    virtual bool IsCFType() {
      if (m_is_cf == eLazyBoolCalculate) {
        const char *class_name = GetClassName().AsCString();
        if (class_name && *class_name)
          m_is_cf = (LazyBool)(strcmp(class_name, "__NSCFType") == 0 ||
                               strcmp(class_name, "NSCFType") == 0);
      }
      return (m_is_cf == eLazyBoolYes);
    }

    virtual bool IsValid() = 0;

    virtual bool GetTaggedPointerInfo(uint64_t *info_bits = nullptr,
                                      uint64_t *value_bits = nullptr,
                                      uint64_t *payload = nullptr) = 0;

    virtual uint64_t GetInstanceSize() = 0;

    // use to implement version-specific additional constraints on pointers
    virtual bool CheckPointer(lldb::addr_t value, uint32_t ptr_size) const {
      return true;
    }

    virtual ObjCISA GetISA() = 0;

    // This should return true iff the interface could be completed
    virtual bool
    Describe(std::function<void(ObjCISA)> const &superclass_func,
             std::function<bool(const char *, const char *)> const
                 &instance_method_func,
             std::function<bool(const char *, const char *)> const
                 &class_method_func,
             std::function<bool(const char *, const char *, lldb::addr_t,
                                uint64_t)> const &ivar_func) const {
      return false;
    }

    lldb::TypeSP GetType() { return m_type_wp.lock(); }

    void SetType(const lldb::TypeSP &type_sp) { m_type_wp = type_sp; }

    struct iVarDescriptor {
      ConstString m_name;
      CompilerType m_type;
      uint64_t m_size;
      int32_t m_offset;
    };

    virtual size_t GetNumIVars() { return 0; }

    virtual iVarDescriptor GetIVarAtIndex(size_t idx) {
      return iVarDescriptor();
    }

  protected:
    bool IsPointerValid(lldb::addr_t value, uint32_t ptr_size,
                        bool allow_NULLs = false, bool allow_tagged = false,
                        bool check_version_specific = false) const;

  private:
    LazyBool m_is_kvo;
    LazyBool m_is_cf;
    lldb::TypeWP m_type_wp;
  };

  class EncodingToType {
  public:
    virtual ~EncodingToType();

    virtual CompilerType RealizeType(ClangASTContext &ast_ctx, const char *name,
                                     bool for_expression);
    virtual CompilerType RealizeType(const char *name, bool for_expression);

    virtual CompilerType RealizeType(clang::ASTContext &ast_ctx,
                                     const char *name, bool for_expression) = 0;

  protected:
    std::unique_ptr<ClangASTContext> m_scratch_ast_ctx_ap;
  };

  class ObjCExceptionPrecondition : public Breakpoint::BreakpointPrecondition {
  public:
    ObjCExceptionPrecondition();

    ~ObjCExceptionPrecondition() override = default;

    bool EvaluatePrecondition(StoppointCallbackContext &context) override;
    void GetDescription(Stream &stream, lldb::DescriptionLevel level) override;
    Status ConfigurePrecondition(Args &args) override;

  protected:
    void AddClassName(const char *class_name);

  private:
    std::unordered_set<std::string> m_class_names;
  };

  class TaggedPointerVendor {
  public:
    virtual ~TaggedPointerVendor() = default;

    virtual bool IsPossibleTaggedPointer(lldb::addr_t ptr) = 0;

    virtual ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) = 0;

  protected:
    TaggedPointerVendor() = default;

  private:
    DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendor);
  };

  ~ObjCLanguageRuntime() override;

  virtual TaggedPointerVendor *GetTaggedPointerVendor() { return nullptr; }

  typedef std::shared_ptr<EncodingToType> EncodingToTypeSP;

  virtual EncodingToTypeSP GetEncodingToType();

  virtual ClassDescriptorSP GetClassDescriptor(ValueObject &in_value);

  ClassDescriptorSP GetNonKVOClassDescriptor(ValueObject &in_value);

  virtual ClassDescriptorSP
  GetClassDescriptorFromClassName(const ConstString &class_name);

  virtual ClassDescriptorSP GetClassDescriptorFromISA(ObjCISA isa);

  ClassDescriptorSP GetNonKVOClassDescriptor(ObjCISA isa);

  lldb::LanguageType GetLanguageType() const override {
    return lldb::eLanguageTypeObjC;
  }

  virtual bool IsModuleObjCLibrary(const lldb::ModuleSP &module_sp) = 0;

  virtual bool ReadObjCLibrary(const lldb::ModuleSP &module_sp) = 0;

  virtual bool HasReadObjCLibrary() = 0;

  virtual lldb::ThreadPlanSP GetStepThroughTrampolinePlan(Thread &thread,
                                                          bool stop_others) = 0;

  lldb::addr_t LookupInMethodCache(lldb::addr_t class_addr, lldb::addr_t sel);

  void AddToMethodCache(lldb::addr_t class_addr, lldb::addr_t sel,
                        lldb::addr_t impl_addr);

  TypeAndOrName LookupInClassNameCache(lldb::addr_t class_addr);

  void AddToClassNameCache(lldb::addr_t class_addr, const char *name,
                           lldb::TypeSP type_sp);

  void AddToClassNameCache(lldb::addr_t class_addr,
                           const TypeAndOrName &class_or_type_name);

  lldb::TypeSP LookupInCompleteClassCache(ConstString &name);

  virtual UtilityFunction *CreateObjectChecker(const char *) = 0;

  virtual ObjCRuntimeVersions GetRuntimeVersion() const {
    return ObjCRuntimeVersions::eObjC_VersionUnknown;
  }

  bool IsValidISA(ObjCISA isa) {
    UpdateISAToDescriptorMap();
    return m_isa_to_descriptor.count(isa) > 0;
  }

  virtual void UpdateISAToDescriptorMapIfNeeded() = 0;

  void UpdateISAToDescriptorMap() {
    if (m_process && m_process->GetStopID() != m_isa_to_descriptor_stop_id) {
      UpdateISAToDescriptorMapIfNeeded();
    }
  }

  virtual ObjCISA GetISA(const ConstString &name);

  virtual ConstString GetActualTypeName(ObjCISA isa);

  virtual ObjCISA GetParentClass(ObjCISA isa);

  virtual DeclVendor *GetDeclVendor() { return nullptr; }

  // Finds the byte offset of the child_type ivar in parent_type.  If it can't
  // find the offset, returns LLDB_INVALID_IVAR_OFFSET.

  virtual size_t GetByteOffsetForIvar(CompilerType &parent_qual_type,
                                      const char *ivar_name);

  // Given the name of an Objective-C runtime symbol (e.g., ivar offset
  // symbol), try to determine from the runtime what the value of that symbol
  // would be. Useful when the underlying binary is stripped.
  virtual lldb::addr_t LookupRuntimeSymbol(const ConstString &name) {
    return LLDB_INVALID_ADDRESS;
  }

  bool HasNewLiteralsAndIndexing() {
    if (m_has_new_literals_and_indexing == eLazyBoolCalculate) {
      if (CalculateHasNewLiteralsAndIndexing())
        m_has_new_literals_and_indexing = eLazyBoolYes;
      else
        m_has_new_literals_and_indexing = eLazyBoolNo;
    }

    return (m_has_new_literals_and_indexing == eLazyBoolYes);
  }

  virtual void SymbolsDidLoad(const ModuleList &module_list) {
    m_negative_complete_class_cache.clear();
  }

  bool GetTypeBitSize(const CompilerType &compiler_type,
                      uint64_t &size) override;

protected:
  //------------------------------------------------------------------
  // Classes that inherit from ObjCLanguageRuntime can see and modify these
  //------------------------------------------------------------------
  ObjCLanguageRuntime(Process *process);

  virtual bool CalculateHasNewLiteralsAndIndexing() { return false; }

  bool ISAIsCached(ObjCISA isa) const {
    return m_isa_to_descriptor.find(isa) != m_isa_to_descriptor.end();
  }

  bool AddClass(ObjCISA isa, const ClassDescriptorSP &descriptor_sp) {
    if (isa != 0) {
      m_isa_to_descriptor[isa] = descriptor_sp;
      return true;
    }
    return false;
  }

  bool AddClass(ObjCISA isa, const ClassDescriptorSP &descriptor_sp,
                const char *class_name);

  bool AddClass(ObjCISA isa, const ClassDescriptorSP &descriptor_sp,
                uint32_t class_name_hash) {
    if (isa != 0) {
      m_isa_to_descriptor[isa] = descriptor_sp;
      m_hash_to_isa_map.insert(std::make_pair(class_name_hash, isa));
      return true;
    }
    return false;
  }

private:
  // We keep a map of <Class,Selector>->Implementation so we don't have to call
  // the resolver function over and over.

  // FIXME: We need to watch for the loading of Protocols, and flush the cache
  // for any
  // class that we see so changed.

  struct ClassAndSel {
    ClassAndSel() {
      sel_addr = LLDB_INVALID_ADDRESS;
      class_addr = LLDB_INVALID_ADDRESS;
    }

    ClassAndSel(lldb::addr_t in_sel_addr, lldb::addr_t in_class_addr)
        : class_addr(in_class_addr), sel_addr(in_sel_addr) {}

    bool operator==(const ClassAndSel &rhs) {
      if (class_addr == rhs.class_addr && sel_addr == rhs.sel_addr)
        return true;
      else
        return false;
    }

    bool operator<(const ClassAndSel &rhs) const {
      if (class_addr < rhs.class_addr)
        return true;
      else if (class_addr > rhs.class_addr)
        return false;
      else {
        if (sel_addr < rhs.sel_addr)
          return true;
        else
          return false;
      }
    }

    lldb::addr_t class_addr;
    lldb::addr_t sel_addr;
  };

  typedef std::map<ClassAndSel, lldb::addr_t> MsgImplMap;
  typedef std::map<ObjCISA, ClassDescriptorSP> ISAToDescriptorMap;
  typedef std::multimap<uint32_t, ObjCISA> HashToISAMap;
  typedef ISAToDescriptorMap::iterator ISAToDescriptorIterator;
  typedef HashToISAMap::iterator HashToISAIterator;
  typedef ThreadSafeDenseMap<void *, uint64_t> TypeSizeCache;

  MsgImplMap m_impl_cache;
  LazyBool m_has_new_literals_and_indexing;
  ISAToDescriptorMap m_isa_to_descriptor;
  HashToISAMap m_hash_to_isa_map;
  TypeSizeCache m_type_size_cache;

protected:
  uint32_t m_isa_to_descriptor_stop_id;

  typedef std::map<ConstString, lldb::TypeWP> CompleteClassMap;
  CompleteClassMap m_complete_class_cache;

  struct ConstStringSetHelpers {
    size_t operator()(const ConstString &arg) const // for hashing
    {
      return (size_t)arg.GetCString();
    }
    bool operator()(const ConstString &arg1,
                    const ConstString &arg2) const // for equality
    {
      return arg1.operator==(arg2);
    }
  };
  typedef std::unordered_set<ConstString, ConstStringSetHelpers,
                             ConstStringSetHelpers>
      CompleteClassSet;
  CompleteClassSet m_negative_complete_class_cache;

  ISAToDescriptorIterator GetDescriptorIterator(const ConstString &name);

  friend class ::CommandObjectObjC_ClassTable_Dump;

  std::pair<ISAToDescriptorIterator, ISAToDescriptorIterator>
  GetDescriptorIteratorPair(bool update_if_needed = true);

  void ReadObjCLibraryIfNeeded(const ModuleList &module_list);

  DISALLOW_COPY_AND_ASSIGN(ObjCLanguageRuntime);
};

} // namespace lldb_private

#endif // liblldb_ObjCLanguageRuntime_h_
