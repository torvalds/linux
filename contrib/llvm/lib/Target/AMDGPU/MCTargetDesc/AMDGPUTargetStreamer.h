//===-- AMDGPUTargetStreamer.h - AMDGPU Target Streamer --------*- C++ -*--===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUTARGETSTREAMER_H
#define LLVM_LIB_TARGET_AMDGPU_MCTARGETDESC_AMDGPUTARGETSTREAMER_H

#include "AMDKernelCodeT.h"
#include "llvm/BinaryFormat/MsgPackTypes.h"
#include "llvm/MC/MCStreamer.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/Support/AMDGPUMetadata.h"
#include "llvm/Support/AMDHSAKernelDescriptor.h"

namespace llvm {
#include "AMDGPUPTNote.h"

class DataLayout;
class Function;
class MCELFStreamer;
class MCSymbol;
class MDNode;
class Module;
class Type;

class AMDGPUTargetStreamer : public MCTargetStreamer {
protected:
  MCContext &getContext() const { return Streamer.getContext(); }

public:
  AMDGPUTargetStreamer(MCStreamer &S) : MCTargetStreamer(S) {}

  virtual void EmitDirectiveAMDGCNTarget(StringRef Target) = 0;

  virtual void EmitDirectiveHSACodeObjectVersion(uint32_t Major,
                                                 uint32_t Minor) = 0;

  virtual void EmitDirectiveHSACodeObjectISA(uint32_t Major, uint32_t Minor,
                                             uint32_t Stepping,
                                             StringRef VendorName,
                                             StringRef ArchName) = 0;

  virtual void EmitAMDKernelCodeT(const amd_kernel_code_t &Header) = 0;

  virtual void EmitAMDGPUSymbolType(StringRef SymbolName, unsigned Type) = 0;

  /// \returns True on success, false on failure.
  virtual bool EmitISAVersion(StringRef IsaVersionString) = 0;

  /// \returns True on success, false on failure.
  virtual bool EmitHSAMetadataV2(StringRef HSAMetadataString);

  /// \returns True on success, false on failure.
  virtual bool EmitHSAMetadataV3(StringRef HSAMetadataString);

  /// Emit HSA Metadata
  ///
  /// When \p Strict is true, known metadata elements must already be
  /// well-typed. When \p Strict is false, known types are inferred and
  /// the \p HSAMetadata structure is updated with the correct types.
  ///
  /// \returns True on success, false on failure.
  virtual bool EmitHSAMetadata(std::shared_ptr<msgpack::Node> &HSAMetadata,
                               bool Strict) = 0;

  /// \returns True on success, false on failure.
  virtual bool EmitHSAMetadata(const AMDGPU::HSAMD::Metadata &HSAMetadata) = 0;

  /// \returns True on success, false on failure.
  virtual bool EmitPALMetadata(const AMDGPU::PALMD::Metadata &PALMetadata) = 0;

  virtual void EmitAmdhsaKernelDescriptor(
      const MCSubtargetInfo &STI, StringRef KernelName,
      const amdhsa::kernel_descriptor_t &KernelDescriptor, uint64_t NextVGPR,
      uint64_t NextSGPR, bool ReserveVCC, bool ReserveFlatScr,
      bool ReserveXNACK) = 0;

  static StringRef getArchNameFromElfMach(unsigned ElfMach);
  static unsigned getElfMach(StringRef GPU);
};

class AMDGPUTargetAsmStreamer final : public AMDGPUTargetStreamer {
  formatted_raw_ostream &OS;
public:
  AMDGPUTargetAsmStreamer(MCStreamer &S, formatted_raw_ostream &OS);

  void EmitDirectiveAMDGCNTarget(StringRef Target) override;

  void EmitDirectiveHSACodeObjectVersion(uint32_t Major,
                                         uint32_t Minor) override;

  void EmitDirectiveHSACodeObjectISA(uint32_t Major, uint32_t Minor,
                                     uint32_t Stepping, StringRef VendorName,
                                     StringRef ArchName) override;

  void EmitAMDKernelCodeT(const amd_kernel_code_t &Header) override;

  void EmitAMDGPUSymbolType(StringRef SymbolName, unsigned Type) override;

  /// \returns True on success, false on failure.
  bool EmitISAVersion(StringRef IsaVersionString) override;

  /// \returns True on success, false on failure.
  bool EmitHSAMetadata(std::shared_ptr<msgpack::Node> &HSAMetadata,
                       bool Strict) override;

  /// \returns True on success, false on failure.
  bool EmitHSAMetadata(const AMDGPU::HSAMD::Metadata &HSAMetadata) override;

  /// \returns True on success, false on failure.
  bool EmitPALMetadata(const AMDGPU::PALMD::Metadata &PALMetadata) override;

  void EmitAmdhsaKernelDescriptor(
      const MCSubtargetInfo &STI, StringRef KernelName,
      const amdhsa::kernel_descriptor_t &KernelDescriptor, uint64_t NextVGPR,
      uint64_t NextSGPR, bool ReserveVCC, bool ReserveFlatScr,
      bool ReserveXNACK) override;
};

class AMDGPUTargetELFStreamer final : public AMDGPUTargetStreamer {
  MCStreamer &Streamer;

  void EmitNote(StringRef Name, const MCExpr *DescSize, unsigned NoteType,
                function_ref<void(MCELFStreamer &)> EmitDesc);

public:
  AMDGPUTargetELFStreamer(MCStreamer &S, const MCSubtargetInfo &STI);

  MCELFStreamer &getStreamer();

  void EmitDirectiveAMDGCNTarget(StringRef Target) override;

  void EmitDirectiveHSACodeObjectVersion(uint32_t Major,
                                         uint32_t Minor) override;

  void EmitDirectiveHSACodeObjectISA(uint32_t Major, uint32_t Minor,
                                     uint32_t Stepping, StringRef VendorName,
                                     StringRef ArchName) override;

  void EmitAMDKernelCodeT(const amd_kernel_code_t &Header) override;

  void EmitAMDGPUSymbolType(StringRef SymbolName, unsigned Type) override;

  /// \returns True on success, false on failure.
  bool EmitISAVersion(StringRef IsaVersionString) override;

  /// \returns True on success, false on failure.
  bool EmitHSAMetadata(std::shared_ptr<msgpack::Node> &HSAMetadata,
                       bool Strict) override;

  /// \returns True on success, false on failure.
  bool EmitHSAMetadata(const AMDGPU::HSAMD::Metadata &HSAMetadata) override;

  /// \returns True on success, false on failure.
  bool EmitPALMetadata(const AMDGPU::PALMD::Metadata &PALMetadata) override;

  void EmitAmdhsaKernelDescriptor(
      const MCSubtargetInfo &STI, StringRef KernelName,
      const amdhsa::kernel_descriptor_t &KernelDescriptor, uint64_t NextVGPR,
      uint64_t NextSGPR, bool ReserveVCC, bool ReserveFlatScr,
      bool ReserveXNACK) override;
};

}
#endif
