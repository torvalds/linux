//===--- HexagonIICHVX.td -------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def CVI_GATHER_PSEUDO : InstrItinClass;
def CVI_VA            : InstrItinClass;

class HVXItin {
  list<InstrItinData> HVXItin_list = [
    InstrItinData<CVI_VA,
      [InstrStage<1, [SLOT0,SLOT1,SLOT2,SLOT3], 0>,
       InstrStage<1, [CVI_XLANE,CVI_SHIFT, CVI_MPY0, CVI_MPY1]>],
      [9, 7, 7, 7], [HVX_FWD, HVX_FWD, HVX_FWD]>,

    // Used by Gather Pseudo Instructions which are expanded into
    // V6_vgather* and V6_vS32b_new_ai. Even though these instructions
    // use CVI_ST resource, it's not included below to avoid having more than
    // 4 InstrStages and thus changing 'MaxResTerms' to 5.
    InstrItinData <CVI_GATHER_PSEUDO,
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_LD], 0>, InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>]>];
}
