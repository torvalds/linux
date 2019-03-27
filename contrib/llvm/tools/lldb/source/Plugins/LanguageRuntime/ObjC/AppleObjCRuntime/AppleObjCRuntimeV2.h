//===-- AppleObjCRuntimeV2.h ------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AppleObjCRuntimeV2_h_
#define liblldb_AppleObjCRuntimeV2_h_

#include <map>
#include <memory>
#include <mutex>

#include "AppleObjCRuntime.h"
#include "lldb/Target/ObjCLanguageRuntime.h"
#include "lldb/lldb-private.h"

class RemoteNXMapTable;

namespace lldb_private {

class AppleObjCRuntimeV2 : public AppleObjCRuntime {
public:
  ~AppleObjCRuntimeV2() override = default;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::LanguageRuntime *
  CreateInstance(Process *process, lldb::LanguageType language);

  static lldb_private::ConstString GetPluginNameStatic();

  static bool classof(const ObjCLanguageRuntime *runtime) {
    switch (runtime->GetRuntimeVersion()) {
    case ObjCRuntimeVersions::eAppleObjC_V2:
      return true;
    default:
      return false;
    }
  }

  // These are generic runtime functions:
  bool GetDynamicTypeAndAddress(ValueObject &in_value,
                                lldb::DynamicValueType use_dynamic,
                                TypeAndOrName &class_type_or_name,
                                Address &address,
                                Value::ValueType &value_type) override;

  UtilityFunction *CreateObjectChecker(const char *) override;

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

  ObjCRuntimeVersions GetRuntimeVersion() const override {
    return ObjCRuntimeVersions::eAppleObjC_V2;
  }

  size_t GetByteOffsetForIvar(CompilerType &parent_qual_type,
                              const char *ivar_name) override;

  void UpdateISAToDescriptorMapIfNeeded() override;

  ConstString GetActualTypeName(ObjCLanguageRuntime::ObjCISA isa) override;

  ClassDescriptorSP GetClassDescriptor(ValueObject &in_value) override;

  ClassDescriptorSP GetClassDescriptorFromISA(ObjCISA isa) override;

  DeclVendor *GetDeclVendor() override;

  lldb::addr_t LookupRuntimeSymbol(const ConstString &name) override;

  EncodingToTypeSP GetEncodingToType() override;

  bool IsTaggedPointer(lldb::addr_t ptr) override;

  TaggedPointerVendor *GetTaggedPointerVendor() override {
    return m_tagged_pointer_vendor_ap.get();
  }

  lldb::addr_t GetTaggedPointerObfuscator();

  void GetValuesForGlobalCFBooleans(lldb::addr_t &cf_true,
                                    lldb::addr_t &cf_false) override;

  // none of these are valid ISAs - we use them to infer the type
  // of tagged pointers - if we have something meaningful to say
  // we report an actual type - otherwise, we just say tagged
  // there is no connection between the values here and the tagged pointers map
  static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA = 1;
  static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSAtom = 2;
  static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSNumber = 3;
  static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSDateTS = 4;
  static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSManagedObject =
      5;
  static const ObjCLanguageRuntime::ObjCISA g_objc_Tagged_ISA_NSDate = 6;

protected:
  lldb::BreakpointResolverSP CreateExceptionResolver(Breakpoint *bkpt,
                                                     bool catch_bp,
                                                     bool throw_bp) override;

private:
  class HashTableSignature {
  public:
    HashTableSignature();

    bool NeedsUpdate(Process *process, AppleObjCRuntimeV2 *runtime,
                     RemoteNXMapTable &hash_table);

    void UpdateSignature(const RemoteNXMapTable &hash_table);

  protected:
    uint32_t m_count;
    uint32_t m_num_buckets;
    lldb::addr_t m_buckets_ptr;
  };

  class NonPointerISACache {
  public:
    static NonPointerISACache *
    CreateInstance(AppleObjCRuntimeV2 &runtime,
                   const lldb::ModuleSP &objc_module_sp);

    ObjCLanguageRuntime::ClassDescriptorSP GetClassDescriptor(ObjCISA isa);

  private:
    NonPointerISACache(AppleObjCRuntimeV2 &runtime,
                       const lldb::ModuleSP &objc_module_sp,
                       uint64_t objc_debug_isa_class_mask,
                       uint64_t objc_debug_isa_magic_mask,
                       uint64_t objc_debug_isa_magic_value,
                       uint64_t objc_debug_indexed_isa_magic_mask,
                       uint64_t objc_debug_indexed_isa_magic_value,
                       uint64_t objc_debug_indexed_isa_index_mask,
                       uint64_t objc_debug_indexed_isa_index_shift,
                       lldb::addr_t objc_indexed_classes);

    bool EvaluateNonPointerISA(ObjCISA isa, ObjCISA &ret_isa);

    AppleObjCRuntimeV2 &m_runtime;
    std::map<ObjCISA, ObjCLanguageRuntime::ClassDescriptorSP> m_cache;
    lldb::ModuleWP m_objc_module_wp;
    uint64_t m_objc_debug_isa_class_mask;
    uint64_t m_objc_debug_isa_magic_mask;
    uint64_t m_objc_debug_isa_magic_value;

    uint64_t m_objc_debug_indexed_isa_magic_mask;
    uint64_t m_objc_debug_indexed_isa_magic_value;
    uint64_t m_objc_debug_indexed_isa_index_mask;
    uint64_t m_objc_debug_indexed_isa_index_shift;
    lldb::addr_t m_objc_indexed_classes;

    std::vector<lldb::addr_t> m_indexed_isa_cache;

    friend class AppleObjCRuntimeV2;

    DISALLOW_COPY_AND_ASSIGN(NonPointerISACache);
  };

  class TaggedPointerVendorV2
      : public ObjCLanguageRuntime::TaggedPointerVendor {
  public:
    ~TaggedPointerVendorV2() override = default;

    static TaggedPointerVendorV2 *
    CreateInstance(AppleObjCRuntimeV2 &runtime,
                   const lldb::ModuleSP &objc_module_sp);

  protected:
    AppleObjCRuntimeV2 &m_runtime;

    TaggedPointerVendorV2(AppleObjCRuntimeV2 &runtime)
        : TaggedPointerVendor(), m_runtime(runtime) {}

  private:
    DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendorV2);
  };

  class TaggedPointerVendorRuntimeAssisted : public TaggedPointerVendorV2 {
  public:
    bool IsPossibleTaggedPointer(lldb::addr_t ptr) override;

    ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) override;

  protected:
    TaggedPointerVendorRuntimeAssisted(
        AppleObjCRuntimeV2 &runtime, uint64_t objc_debug_taggedpointer_mask,
        uint32_t objc_debug_taggedpointer_slot_shift,
        uint32_t objc_debug_taggedpointer_slot_mask,
        uint32_t objc_debug_taggedpointer_payload_lshift,
        uint32_t objc_debug_taggedpointer_payload_rshift,
        lldb::addr_t objc_debug_taggedpointer_classes);

    typedef std::map<uint8_t, ObjCLanguageRuntime::ClassDescriptorSP> Cache;
    typedef Cache::iterator CacheIterator;
    Cache m_cache;
    uint64_t m_objc_debug_taggedpointer_mask;
    uint32_t m_objc_debug_taggedpointer_slot_shift;
    uint32_t m_objc_debug_taggedpointer_slot_mask;
    uint32_t m_objc_debug_taggedpointer_payload_lshift;
    uint32_t m_objc_debug_taggedpointer_payload_rshift;
    lldb::addr_t m_objc_debug_taggedpointer_classes;

    friend class AppleObjCRuntimeV2::TaggedPointerVendorV2;

    DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendorRuntimeAssisted);
  };

  class TaggedPointerVendorExtended
      : public TaggedPointerVendorRuntimeAssisted {
  public:
    ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) override;

  protected:
    TaggedPointerVendorExtended(
        AppleObjCRuntimeV2 &runtime, uint64_t objc_debug_taggedpointer_mask,
        uint64_t objc_debug_taggedpointer_ext_mask,
        uint32_t objc_debug_taggedpointer_slot_shift,
        uint32_t objc_debug_taggedpointer_ext_slot_shift,
        uint32_t objc_debug_taggedpointer_slot_mask,
        uint32_t objc_debug_taggedpointer_ext_slot_mask,
        uint32_t objc_debug_taggedpointer_payload_lshift,
        uint32_t objc_debug_taggedpointer_payload_rshift,
        uint32_t objc_debug_taggedpointer_ext_payload_lshift,
        uint32_t objc_debug_taggedpointer_ext_payload_rshift,
        lldb::addr_t objc_debug_taggedpointer_classes,
        lldb::addr_t objc_debug_taggedpointer_ext_classes);

    bool IsPossibleExtendedTaggedPointer(lldb::addr_t ptr);

    typedef std::map<uint8_t, ObjCLanguageRuntime::ClassDescriptorSP> Cache;
    typedef Cache::iterator CacheIterator;
    Cache m_ext_cache;
    uint64_t m_objc_debug_taggedpointer_ext_mask;
    uint32_t m_objc_debug_taggedpointer_ext_slot_shift;
    uint32_t m_objc_debug_taggedpointer_ext_slot_mask;
    uint32_t m_objc_debug_taggedpointer_ext_payload_lshift;
    uint32_t m_objc_debug_taggedpointer_ext_payload_rshift;
    lldb::addr_t m_objc_debug_taggedpointer_ext_classes;

    friend class AppleObjCRuntimeV2::TaggedPointerVendorV2;

    DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendorExtended);
  };

  class TaggedPointerVendorLegacy : public TaggedPointerVendorV2 {
  public:
    bool IsPossibleTaggedPointer(lldb::addr_t ptr) override;

    ObjCLanguageRuntime::ClassDescriptorSP
    GetClassDescriptor(lldb::addr_t ptr) override;

  protected:
    TaggedPointerVendorLegacy(AppleObjCRuntimeV2 &runtime)
        : TaggedPointerVendorV2(runtime) {}

    friend class AppleObjCRuntimeV2::TaggedPointerVendorV2;

    DISALLOW_COPY_AND_ASSIGN(TaggedPointerVendorLegacy);
  };

  struct DescriptorMapUpdateResult {
    bool m_update_ran;
    uint32_t m_num_found;

    DescriptorMapUpdateResult(bool ran, uint32_t found) {
      m_update_ran = ran;
      m_num_found = found;
    }

    static DescriptorMapUpdateResult Fail() { return {false, 0}; }

    static DescriptorMapUpdateResult Success(uint32_t found) {
      return {true, found};
    }
  };

  AppleObjCRuntimeV2(Process *process, const lldb::ModuleSP &objc_module_sp);

  ObjCISA GetPointerISA(ObjCISA isa);

  lldb::addr_t GetISAHashTablePointer();

  bool UpdateISAToDescriptorMapFromMemory(RemoteNXMapTable &hash_table);

  DescriptorMapUpdateResult
  UpdateISAToDescriptorMapDynamic(RemoteNXMapTable &hash_table);

  uint32_t ParseClassInfoArray(const lldb_private::DataExtractor &data,
                               uint32_t num_class_infos);

  DescriptorMapUpdateResult UpdateISAToDescriptorMapSharedCache();

  enum class SharedCacheWarningReason {
    eExpressionExecutionFailure,
    eNotEnoughClassesRead
  };

  void WarnIfNoClassesCached(SharedCacheWarningReason reason);

  lldb::addr_t GetSharedCacheReadOnlyAddress();

  bool GetCFBooleanValuesIfNeeded();

  friend class ClassDescriptorV2;

  std::unique_ptr<UtilityFunction> m_get_class_info_code;
  lldb::addr_t m_get_class_info_args;
  std::mutex m_get_class_info_args_mutex;

  std::unique_ptr<UtilityFunction> m_get_shared_cache_class_info_code;
  lldb::addr_t m_get_shared_cache_class_info_args;
  std::mutex m_get_shared_cache_class_info_args_mutex;

  std::unique_ptr<DeclVendor> m_decl_vendor_ap;
  lldb::addr_t m_tagged_pointer_obfuscator;
  lldb::addr_t m_isa_hash_table_ptr;
  HashTableSignature m_hash_signature;
  bool m_has_object_getClass;
  bool m_loaded_objc_opt;
  std::unique_ptr<NonPointerISACache> m_non_pointer_isa_cache_ap;
  std::unique_ptr<TaggedPointerVendor> m_tagged_pointer_vendor_ap;
  EncodingToTypeSP m_encoding_to_type_sp;
  bool m_noclasses_warning_emitted;
  llvm::Optional<std::pair<lldb::addr_t, lldb::addr_t>> m_CFBoolean_values;
};

} // namespace lldb_private

#endif // liblldb_AppleObjCRuntimeV2_h_
