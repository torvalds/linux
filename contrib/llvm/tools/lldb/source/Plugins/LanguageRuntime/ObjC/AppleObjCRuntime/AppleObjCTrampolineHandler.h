//===-- AppleObjCTrampolineHandler.h ----------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_AppleObjCTrampolineHandler_h_
#define lldb_AppleObjCTrampolineHandler_h_

#include <map>
#include <mutex>
#include <vector>

#include "lldb/Expression/UtilityFunction.h"
#include "lldb/lldb-public.h"

namespace lldb_private {

class AppleObjCTrampolineHandler {
public:
  AppleObjCTrampolineHandler(const lldb::ProcessSP &process_sp,
                             const lldb::ModuleSP &objc_module_sp);

  ~AppleObjCTrampolineHandler();

  lldb::ThreadPlanSP GetStepThroughDispatchPlan(Thread &thread,
                                                bool stop_others);

  FunctionCaller *GetLookupImplementationFunctionCaller();

  bool AddrIsMsgForward(lldb::addr_t addr) const {
    return (addr == m_msg_forward_addr || addr == m_msg_forward_stret_addr);
  }

  struct DispatchFunction {
  public:
    typedef enum { eFixUpNone, eFixUpFixed, eFixUpToFix } FixUpState;

    const char *name;
    bool stret_return;
    bool is_super;
    bool is_super2;
    FixUpState fixedup;
  };

  lldb::addr_t SetupDispatchFunction(Thread &thread,
                                     ValueList &dispatch_values);

private:
  static const char *g_lookup_implementation_function_name;
  static const char *g_lookup_implementation_with_stret_function_code;
  static const char *g_lookup_implementation_no_stret_function_code;

  class AppleObjCVTables {
  public:
    // These come from objc-gdb.h.
    enum VTableFlags {
      eOBJC_TRAMPOLINE_MESSAGE = (1 << 0), // trampoline acts like objc_msgSend
      eOBJC_TRAMPOLINE_STRET = (1 << 1),   // trampoline is struct-returning
      eOBJC_TRAMPOLINE_VTABLE = (1 << 2)   // trampoline is vtable dispatcher
    };

  private:
    struct VTableDescriptor {
      VTableDescriptor(uint32_t in_flags, lldb::addr_t in_code_start)
          : flags(in_flags), code_start(in_code_start) {}

      uint32_t flags;
      lldb::addr_t code_start;
    };

    class VTableRegion {
    public:
      VTableRegion()
          : m_valid(false), m_owner(NULL), m_header_addr(LLDB_INVALID_ADDRESS),
            m_code_start_addr(0), m_code_end_addr(0), m_next_region(0) {}

      VTableRegion(AppleObjCVTables *owner, lldb::addr_t header_addr);

      void SetUpRegion();

      lldb::addr_t GetNextRegionAddr() { return m_next_region; }

      lldb::addr_t GetCodeStart() { return m_code_start_addr; }

      lldb::addr_t GetCodeEnd() { return m_code_end_addr; }

      uint32_t GetFlagsForVTableAtAddress(lldb::addr_t address) { return 0; }

      bool IsValid() { return m_valid; }

      bool AddressInRegion(lldb::addr_t addr, uint32_t &flags);

      void Dump(Stream &s);

    public:
      bool m_valid;
      AppleObjCVTables *m_owner;
      lldb::addr_t m_header_addr;
      lldb::addr_t m_code_start_addr;
      lldb::addr_t m_code_end_addr;
      std::vector<VTableDescriptor> m_descriptors;
      lldb::addr_t m_next_region;
    };

  public:
    AppleObjCVTables(const lldb::ProcessSP &process_sp,
                     const lldb::ModuleSP &objc_module_sp);

    ~AppleObjCVTables();

    bool InitializeVTableSymbols();

    static bool RefreshTrampolines(void *baton,
                                   StoppointCallbackContext *context,
                                   lldb::user_id_t break_id,
                                   lldb::user_id_t break_loc_id);
    bool ReadRegions();

    bool ReadRegions(lldb::addr_t region_addr);

    bool IsAddressInVTables(lldb::addr_t addr, uint32_t &flags);

    lldb::ProcessSP GetProcessSP() { return m_process_wp.lock(); }

  private:
    lldb::ProcessWP m_process_wp;
    typedef std::vector<VTableRegion> region_collection;
    lldb::addr_t m_trampoline_header;
    lldb::break_id_t m_trampolines_changed_bp_id;
    region_collection m_regions;
    lldb::ModuleSP m_objc_module_sp;
  };

  static const DispatchFunction g_dispatch_functions[];

  typedef std::map<lldb::addr_t, int> MsgsendMap; // This table maps an dispatch
                                                  // fn address to the index in
                                                  // g_dispatch_functions
  MsgsendMap m_msgSend_map;
  lldb::ProcessWP m_process_wp;
  lldb::ModuleSP m_objc_module_sp;
  const char *m_lookup_implementation_function_code;
  std::unique_ptr<UtilityFunction> m_impl_code;
  std::mutex m_impl_function_mutex;
  lldb::addr_t m_impl_fn_addr;
  lldb::addr_t m_impl_stret_fn_addr;
  lldb::addr_t m_msg_forward_addr;
  lldb::addr_t m_msg_forward_stret_addr;
  std::unique_ptr<AppleObjCVTables> m_vtables_ap;
};

} // namespace lldb_private

#endif // lldb_AppleObjCTrampolineHandler_h_
