//=====-- NVPTXTargetStreamer.h - NVPTX Target Streamer ------*- C++ -*--=====//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXTARGETSTREAMER_H
#define LLVM_LIB_TARGET_NVPTX_MCTARGETDESC_NVPTXTARGETSTREAMER_H

#include "llvm/MC/MCStreamer.h"

namespace llvm {
class MCSection;

/// Implments NVPTX-specific streamer.
class NVPTXTargetStreamer : public MCTargetStreamer {
private:
  SmallVector<std::string, 4> DwarfFiles;

public:
  NVPTXTargetStreamer(MCStreamer &S);
  ~NVPTXTargetStreamer() override;

  /// Outputs the list of the DWARF '.file' directives to the streamer.
  void outputDwarfFileDirectives();

  /// Record DWARF file directives for later output.
  /// According to PTX ISA, CUDA Toolkit documentation, 11.5.3. Debugging
  /// Directives: .file
  /// (http://docs.nvidia.com/cuda/parallel-thread-execution/index.html#debugging-directives-file),
  /// The .file directive is allowed only in the outermost scope, i.e., at the
  /// same level as kernel and device function declarations. Also, the order of
  /// the .loc and .file directive does not matter, .file directives may follow
  /// the .loc directives where the file is referenced.
  /// LLVM emits .file directives immediately the location debug info is
  /// emitted, i.e. they may be emitted inside functions. We gather all these
  /// directives and emit them outside of the sections and, thus, outside of the
  /// functions.
  void emitDwarfFileDirective(StringRef Directive) override;
  void changeSection(const MCSection *CurSection, MCSection *Section,
                     const MCExpr *SubSection, raw_ostream &OS) override;
  /// Emit the bytes in \p Data into the output.
  ///
  /// This is used to emit bytes in \p Data as sequence of .byte directives.
  void emitRawBytes(StringRef Data) override;
};

} // end namespace llvm

#endif
