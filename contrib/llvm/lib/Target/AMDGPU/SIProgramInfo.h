//===--- SIProgramInfo.h ----------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Defines struct to track resource usage for kernels and entry functions.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_AMDGPU_SIPROGRAMINFO_H
#define LLVM_LIB_TARGET_AMDGPU_SIPROGRAMINFO_H

namespace llvm {

/// Track resource usage for kernels / entry functions.
struct SIProgramInfo {
    // Fields set in PGM_RSRC1 pm4 packet.
    uint32_t VGPRBlocks = 0;
    uint32_t SGPRBlocks = 0;
    uint32_t Priority = 0;
    uint32_t FloatMode = 0;
    uint32_t Priv = 0;
    uint32_t DX10Clamp = 0;
    uint32_t DebugMode = 0;
    uint32_t IEEEMode = 0;
    uint64_t ScratchSize = 0;

    uint64_t ComputePGMRSrc1 = 0;

    // Fields set in PGM_RSRC2 pm4 packet.
    uint32_t LDSBlocks = 0;
    uint32_t ScratchBlocks = 0;

    uint64_t ComputePGMRSrc2 = 0;

    uint32_t NumVGPR = 0;
    uint32_t NumSGPR = 0;
    uint32_t LDSSize = 0;
    bool FlatUsed = false;

    // Number of SGPRs that meets number of waves per execution unit request.
    uint32_t NumSGPRsForWavesPerEU = 0;

    // Number of VGPRs that meets number of waves per execution unit request.
    uint32_t NumVGPRsForWavesPerEU = 0;

    // Fixed SGPR number used to hold wave scratch offset for entire kernel
    // execution, or std::numeric_limits<uint16_t>::max() if the register is not
    // used or not known.
    uint16_t DebuggerWavefrontPrivateSegmentOffsetSGPR =
        std::numeric_limits<uint16_t>::max();

    // Fixed SGPR number of the first 4 SGPRs used to hold scratch V# for entire
    // kernel execution, or std::numeric_limits<uint16_t>::max() if the register
    // is not used or not known.
    uint16_t DebuggerPrivateSegmentBufferSGPR =
        std::numeric_limits<uint16_t>::max();

    // Whether there is recursion, dynamic allocas, indirect calls or some other
    // reason there may be statically unknown stack usage.
    bool DynamicCallStack = false;

    // Bonus information for debugging.
    bool VCCUsed = false;

    SIProgramInfo() = default;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_AMDGPU_SIPROGRAMINFO_H
