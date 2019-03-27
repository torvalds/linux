//==- HexagonPatternsV65.td -------------------------------*- tablegen -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

multiclass vgathermh<RegisterClass RC> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1, mayStore = 1 in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins  IntRegs:$_dst_, IntRegs:$Rt,
                                 ModRegs:$Mu, RC:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermw<RegisterClass RC> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1, mayStore = 1 in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, IntRegs:$Rt,
                                ModRegs:$Mu, RC:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermhw<RegisterClass RC> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1, mayStore = 1 in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, IntRegs:$Rt,
                                ModRegs:$Mu, RC:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

defm V6_vgathermh_pseudo  : vgathermh<HvxVR>;
defm V6_vgathermw_pseudo  : vgathermw<HvxVR>;
defm V6_vgathermhw_pseudo  : vgathermhw<HvxWR>;

multiclass vgathermhq<RegisterClass RC1, RegisterClass RC2> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1, mayStore = 1 in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins  IntRegs:$_dst_, RC2:$Vq, IntRegs:$Rt,
                                 ModRegs:$Mu, RC1:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermwq<RegisterClass RC1, RegisterClass RC2> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1, mayStore = 1 in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, RC2:$Vq, IntRegs:$Rt,
                                ModRegs:$Mu, RC1:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

multiclass vgathermhwq<RegisterClass RC1, RegisterClass RC2> {
  let isCodeGenOnly = 1, isPseudo = 1, mayLoad = 1, mayStore = 1 in
  def NAME : CVI_GATHER_TMP_LD_Resource_NoOpcode<(outs ),
                           (ins IntRegs:$_dst_, RC2:$Vq, IntRegs:$Rt,
                                ModRegs:$Mu, RC1:$Vv),
                           ".error \"should not emit\" ",
                           []>;
}

defm V6_vgathermhq_pseudo  : vgathermhq<HvxVR, HvxQR>;
defm V6_vgathermwq_pseudo  : vgathermwq<HvxVR, HvxQR>;
defm V6_vgathermhwq_pseudo  : vgathermhwq<HvxWR, HvxQR>;
