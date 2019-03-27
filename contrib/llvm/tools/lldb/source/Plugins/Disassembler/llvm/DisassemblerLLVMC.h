//===-- DisassemblerLLVMC.h -------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_DisassemblerLLVMC_h_
#define liblldb_DisassemblerLLVMC_h_

#include <memory>
#include <mutex>
#include <string>

#include "lldb/Core/Address.h"
#include "lldb/Core/Disassembler.h"
#include "lldb/Core/PluginManager.h"

class InstructionLLVMC;

class DisassemblerLLVMC : public lldb_private::Disassembler {
public:
  DisassemblerLLVMC(const lldb_private::ArchSpec &arch,
                    const char *flavor /* = NULL */);

  ~DisassemblerLLVMC() override;

  //------------------------------------------------------------------
  // Static Functions
  //------------------------------------------------------------------
  static void Initialize();

  static void Terminate();

  static lldb_private::ConstString GetPluginNameStatic();

  static lldb_private::Disassembler *
  CreateInstance(const lldb_private::ArchSpec &arch, const char *flavor);

  size_t DecodeInstructions(const lldb_private::Address &base_addr,
                            const lldb_private::DataExtractor &data,
                            lldb::offset_t data_offset, size_t num_instructions,
                            bool append, bool data_from_file) override;

  //------------------------------------------------------------------
  // PluginInterface protocol
  //------------------------------------------------------------------
  lldb_private::ConstString GetPluginName() override;

  uint32_t GetPluginVersion() override;

protected:
  friend class InstructionLLVMC;

  bool FlavorValidForArchSpec(const lldb_private::ArchSpec &arch,
                              const char *flavor) override;

  bool IsValid() const;

  int OpInfo(uint64_t PC, uint64_t Offset, uint64_t Size, int TagType,
             void *TagBug);

  const char *SymbolLookup(uint64_t ReferenceValue, uint64_t *ReferenceType,
                           uint64_t ReferencePC, const char **ReferenceName);

  static int OpInfoCallback(void *DisInfo, uint64_t PC, uint64_t Offset,
                            uint64_t Size, int TagType, void *TagBug);

  static const char *SymbolLookupCallback(void *DisInfo,
                                          uint64_t ReferenceValue,
                                          uint64_t *ReferenceType,
                                          uint64_t ReferencePC,
                                          const char **ReferenceName);

  const lldb_private::ExecutionContext *m_exe_ctx;
  InstructionLLVMC *m_inst;
  std::mutex m_mutex;
  bool m_data_from_file;

  // Since we need to make two actual MC Disassemblers for ARM (ARM & THUMB),
  // and there's a bit of goo to set up and own in the MC disassembler world,
  // this class was added to manage the actual disassemblers.
  class MCDisasmInstance;
  std::unique_ptr<MCDisasmInstance> m_disasm_up;
  std::unique_ptr<MCDisasmInstance> m_alternate_disasm_up;
};

#endif // liblldb_DisassemblerLLVM_h_
