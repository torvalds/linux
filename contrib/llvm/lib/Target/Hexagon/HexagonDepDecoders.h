//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Automatically generated file, please consult code owner before editing.
//===----------------------------------------------------------------------===//

// clang-format off

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif

static DecodeStatus s4_0ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<4>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s29_3ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<14>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s8_0ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<8>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s4_3ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<7>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s31_1ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<12>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s3_0ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<3>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s30_2ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<13>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s6_0ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<6>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s6_3ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<9>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s4_1ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<5>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}
static DecodeStatus s4_2ImmDecoder(MCInst &MI, unsigned tmp,
    uint64_t, const void *Decoder) {
  signedDecoder<6>(MI, tmp, Decoder);
  return MCDisassembler::Success;
}

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

// clang-format on
