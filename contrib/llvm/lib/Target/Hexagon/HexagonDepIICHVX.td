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

def tc_04da405a : InstrItinClass;
def tc_05058f6f : InstrItinClass;
def tc_05ac6f98 : InstrItinClass;
def tc_05ca8cfd : InstrItinClass;
def tc_08a4f1b6 : InstrItinClass;
def tc_0b04c6c7 : InstrItinClass;
def tc_0ec46cf9 : InstrItinClass;
def tc_131f1c81 : InstrItinClass;
def tc_1381a97c : InstrItinClass;
def tc_15fdf750 : InstrItinClass;
def tc_16ff9ef8 : InstrItinClass;
def tc_191381c1 : InstrItinClass;
def tc_1ad8a370 : InstrItinClass;
def tc_1ba8a0cd : InstrItinClass;
def tc_20a4bbec : InstrItinClass;
def tc_257f6f7c : InstrItinClass;
def tc_26a377fe : InstrItinClass;
def tc_2c745bb8 : InstrItinClass;
def tc_2d4051cd : InstrItinClass;
def tc_2e8f5f6e : InstrItinClass;
def tc_309dbb4f : InstrItinClass;
def tc_3904b926 : InstrItinClass;
def tc_3aacf4a8 : InstrItinClass;
def tc_3ad719fb : InstrItinClass;
def tc_3c56e5ce : InstrItinClass;
def tc_3ce09744 : InstrItinClass;
def tc_3e2aaafc : InstrItinClass;
def tc_447d9895 : InstrItinClass;
def tc_453fe68d : InstrItinClass;
def tc_46d6c3e0 : InstrItinClass;
def tc_51d0ecc3 : InstrItinClass;
def tc_52447ecc : InstrItinClass;
def tc_540c3da3 : InstrItinClass;
def tc_54a0dc47 : InstrItinClass;
def tc_561aaa58 : InstrItinClass;
def tc_56c4f9fe : InstrItinClass;
def tc_56e64202 : InstrItinClass;
def tc_58d21193 : InstrItinClass;
def tc_5bf8afbb : InstrItinClass;
def tc_61bf7c03 : InstrItinClass;
def tc_649072c2 : InstrItinClass;
def tc_660769f1 : InstrItinClass;
def tc_663c80a7 : InstrItinClass;
def tc_6942b6e0 : InstrItinClass;
def tc_6e7fa133 : InstrItinClass;
def tc_71646d06 : InstrItinClass;
def tc_7177e272 : InstrItinClass;
def tc_718b5c53 : InstrItinClass;
def tc_7273323b : InstrItinClass;
def tc_7417e785 : InstrItinClass;
def tc_767c4e9d : InstrItinClass;
def tc_7e6a3e89 : InstrItinClass;
def tc_8772086c : InstrItinClass;
def tc_87adc037 : InstrItinClass;
def tc_8e420e4d : InstrItinClass;
def tc_90bcc1db : InstrItinClass;
def tc_933f2b39 : InstrItinClass;
def tc_946013d8 : InstrItinClass;
def tc_9d1dc972 : InstrItinClass;
def tc_9f363d21 : InstrItinClass;
def tc_a02a10a8 : InstrItinClass;
def tc_a0dbea28 : InstrItinClass;
def tc_a7e6707d : InstrItinClass;
def tc_ab23f776 : InstrItinClass;
def tc_abe8c3b2 : InstrItinClass;
def tc_ac4046bc : InstrItinClass;
def tc_af25efd9 : InstrItinClass;
def tc_b091f1c6 : InstrItinClass;
def tc_b28e51aa : InstrItinClass;
def tc_b4416217 : InstrItinClass;
def tc_b9db8205 : InstrItinClass;
def tc_c0749f3c : InstrItinClass;
def tc_c127de3a : InstrItinClass;
def tc_c4edf264 : InstrItinClass;
def tc_c5dba46e : InstrItinClass;
def tc_c7039829 : InstrItinClass;
def tc_cd94bfe0 : InstrItinClass;
def tc_d8287c14 : InstrItinClass;
def tc_db5555f3 : InstrItinClass;
def tc_dd5b0695 : InstrItinClass;
def tc_df80eeb0 : InstrItinClass;
def tc_e2d2e9e5 : InstrItinClass;
def tc_e35c1e93 : InstrItinClass;
def tc_e3f68a46 : InstrItinClass;
def tc_e675c45a : InstrItinClass;
def tc_e699ae41 : InstrItinClass;
def tc_e8797b98 : InstrItinClass;
def tc_e99d4c2e : InstrItinClass;
def tc_f1de44ef : InstrItinClass;
def tc_f21e8abb : InstrItinClass;
def tc_fd7610da : InstrItinClass;

class DepHVXItinV55 {
  list<InstrItinData> DepHVXItinV55_list = [
    InstrItinData <tc_04da405a, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_05058f6f, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ac6f98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ca8cfd, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_08a4f1b6, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_0b04c6c7, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_0ec46cf9, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_131f1c81, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1381a97c, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [],
      []>,

    InstrItinData <tc_15fdf750, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_16ff9ef8, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_191381c1, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 7, 1, 2, 7],
      [Hex_FWD, HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1ad8a370, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_1ba8a0cd, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_20a4bbec, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_257f6f7c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_26a377fe, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2c745bb8, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_2d4051cd, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 7, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2e8f5f6e, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 7, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_309dbb4f, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_3904b926, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3aacf4a8, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_3ad719fb, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3c56e5ce, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3ce09744, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3e2aaafc, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_447d9895, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_453fe68d, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_46d6c3e0, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_51d0ecc3, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_52447ecc, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_540c3da3, /*SLOT0,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [4, 7, 1],
      [Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_54a0dc47, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_561aaa58, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_56c4f9fe, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_56e64202, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_58d21193, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_5bf8afbb, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_61bf7c03, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_649072c2, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_660769f1, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_663c80a7, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_6942b6e0, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_6e7fa133, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_71646d06, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7177e272, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_718b5c53, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9],
      [HVX_FWD]>,

    InstrItinData <tc_7273323b, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7417e785, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_767c4e9d, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_7e6a3e89, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_8772086c, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_87adc037, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_8e420e4d, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_90bcc1db, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_933f2b39, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_946013d8, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_9d1dc972, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_9f363d21, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_a02a10a8, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_a0dbea28, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_a7e6707d, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ab23f776, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_abe8c3b2, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ac4046bc, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_af25efd9, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 2, 7, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b091f1c6, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_b28e51aa, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [2],
      [Hex_FWD]>,

    InstrItinData <tc_b4416217, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b9db8205, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c0749f3c, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c127de3a, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_c4edf264, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_c5dba46e, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_c7039829, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_cd94bfe0, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_d8287c14, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_db5555f3, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_dd5b0695, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_df80eeb0, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e2d2e9e5, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e35c1e93, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e3f68a46, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3],
      [HVX_FWD]>,

    InstrItinData <tc_e675c45a, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e699ae41, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e8797b98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e99d4c2e, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_f1de44ef, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_f21e8abb, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_fd7610da, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>
  ];
}

class DepHVXItinV60 {
  list<InstrItinData> DepHVXItinV60_list = [
    InstrItinData <tc_04da405a, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_05058f6f, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ac6f98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ca8cfd, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_08a4f1b6, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_0b04c6c7, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_0ec46cf9, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_131f1c81, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1381a97c, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [],
      []>,

    InstrItinData <tc_15fdf750, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_16ff9ef8, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_191381c1, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 7, 1, 2, 7],
      [Hex_FWD, HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1ad8a370, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_1ba8a0cd, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_20a4bbec, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_257f6f7c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_26a377fe, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2c745bb8, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_2d4051cd, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 7, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2e8f5f6e, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 7, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_309dbb4f, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_3904b926, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3aacf4a8, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_3ad719fb, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3c56e5ce, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3ce09744, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3e2aaafc, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_447d9895, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_453fe68d, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_46d6c3e0, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_51d0ecc3, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_52447ecc, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_540c3da3, /*SLOT0,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [4, 7, 1],
      [Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_54a0dc47, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_561aaa58, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_56c4f9fe, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_56e64202, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_58d21193, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_5bf8afbb, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_61bf7c03, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_649072c2, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_660769f1, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_663c80a7, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_6942b6e0, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_6e7fa133, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_71646d06, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7177e272, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_718b5c53, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9],
      [HVX_FWD]>,

    InstrItinData <tc_7273323b, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7417e785, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_767c4e9d, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_7e6a3e89, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_8772086c, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_87adc037, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_8e420e4d, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_90bcc1db, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_933f2b39, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_946013d8, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_9d1dc972, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_9f363d21, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_a02a10a8, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_a0dbea28, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_a7e6707d, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ab23f776, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_abe8c3b2, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ac4046bc, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_af25efd9, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 2, 7, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b091f1c6, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_b28e51aa, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [2],
      [Hex_FWD]>,

    InstrItinData <tc_b4416217, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b9db8205, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c0749f3c, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c127de3a, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_c4edf264, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_c5dba46e, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_c7039829, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_cd94bfe0, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_d8287c14, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_db5555f3, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_dd5b0695, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_df80eeb0, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e2d2e9e5, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e35c1e93, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e3f68a46, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3],
      [HVX_FWD]>,

    InstrItinData <tc_e675c45a, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e699ae41, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e8797b98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e99d4c2e, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_f1de44ef, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_f21e8abb, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_fd7610da, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>
  ];
}

class DepHVXItinV62 {
  list<InstrItinData> DepHVXItinV62_list = [
    InstrItinData <tc_04da405a, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_05058f6f, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ac6f98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ca8cfd, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_08a4f1b6, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_0b04c6c7, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_0ec46cf9, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_131f1c81, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1381a97c, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [],
      []>,

    InstrItinData <tc_15fdf750, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_16ff9ef8, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_191381c1, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 7, 1, 2, 7],
      [Hex_FWD, HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1ad8a370, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_1ba8a0cd, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_20a4bbec, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_257f6f7c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_26a377fe, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2c745bb8, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_2d4051cd, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 7, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2e8f5f6e, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 7, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_309dbb4f, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_3904b926, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3aacf4a8, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_3ad719fb, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3c56e5ce, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3ce09744, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3e2aaafc, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_447d9895, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_453fe68d, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_46d6c3e0, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_51d0ecc3, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_52447ecc, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_540c3da3, /*SLOT0,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [4, 7, 1],
      [Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_54a0dc47, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_561aaa58, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_56c4f9fe, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_56e64202, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_58d21193, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_5bf8afbb, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_61bf7c03, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_649072c2, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_660769f1, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_663c80a7, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_6942b6e0, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_6e7fa133, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_71646d06, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7177e272, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_718b5c53, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9],
      [HVX_FWD]>,

    InstrItinData <tc_7273323b, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7417e785, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_767c4e9d, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_7e6a3e89, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_8772086c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_87adc037, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_8e420e4d, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_90bcc1db, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_933f2b39, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_946013d8, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_9d1dc972, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_9f363d21, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_a02a10a8, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_a0dbea28, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_a7e6707d, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ab23f776, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_abe8c3b2, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ac4046bc, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_af25efd9, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 2, 7, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b091f1c6, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_b28e51aa, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [2],
      [Hex_FWD]>,

    InstrItinData <tc_b4416217, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b9db8205, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c0749f3c, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c127de3a, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_c4edf264, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_c5dba46e, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_c7039829, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_cd94bfe0, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_d8287c14, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_db5555f3, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_dd5b0695, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_df80eeb0, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e2d2e9e5, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e35c1e93, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e3f68a46, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3],
      [HVX_FWD]>,

    InstrItinData <tc_e675c45a, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e699ae41, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e8797b98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e99d4c2e, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_f1de44ef, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_f21e8abb, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_fd7610da, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>
  ];
}

class DepHVXItinV65 {
  list<InstrItinData> DepHVXItinV65_list = [
    InstrItinData <tc_04da405a, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_05058f6f, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ac6f98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ca8cfd, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_08a4f1b6, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_0b04c6c7, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_0ec46cf9, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_131f1c81, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1381a97c, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [],
      []>,

    InstrItinData <tc_15fdf750, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_16ff9ef8, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_191381c1, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 7, 1, 2, 7],
      [Hex_FWD, HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1ad8a370, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_1ba8a0cd, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_20a4bbec, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_257f6f7c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_26a377fe, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2c745bb8, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_2d4051cd, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 7, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2e8f5f6e, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 7, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_309dbb4f, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_3904b926, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3aacf4a8, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_3ad719fb, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3c56e5ce, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3ce09744, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3e2aaafc, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_447d9895, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_453fe68d, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_46d6c3e0, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_51d0ecc3, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_52447ecc, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_540c3da3, /*SLOT0,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [4, 7, 1],
      [Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_54a0dc47, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_561aaa58, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_56c4f9fe, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_56e64202, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_58d21193, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_5bf8afbb, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_61bf7c03, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_649072c2, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_660769f1, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_663c80a7, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_6942b6e0, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_6e7fa133, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_71646d06, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7177e272, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_718b5c53, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9],
      [HVX_FWD]>,

    InstrItinData <tc_7273323b, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7417e785, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_767c4e9d, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_7e6a3e89, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_8772086c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_87adc037, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_8e420e4d, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_90bcc1db, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_933f2b39, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_946013d8, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_9d1dc972, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_9f363d21, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_a02a10a8, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_a0dbea28, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_a7e6707d, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ab23f776, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_abe8c3b2, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ac4046bc, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_af25efd9, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 2, 7, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b091f1c6, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_b28e51aa, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [2],
      [Hex_FWD]>,

    InstrItinData <tc_b4416217, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b9db8205, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c0749f3c, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c127de3a, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_c4edf264, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_c5dba46e, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_c7039829, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_cd94bfe0, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_d8287c14, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_db5555f3, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_dd5b0695, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_df80eeb0, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e2d2e9e5, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e35c1e93, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e3f68a46, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3],
      [HVX_FWD]>,

    InstrItinData <tc_e675c45a, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e699ae41, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e8797b98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e99d4c2e, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_f1de44ef, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_f21e8abb, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_fd7610da, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>
  ];
}

class DepHVXItinV66 {
  list<InstrItinData> DepHVXItinV66_list = [
    InstrItinData <tc_04da405a, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_05058f6f, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ac6f98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_05ca8cfd, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_08a4f1b6, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_0b04c6c7, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_0ec46cf9, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_131f1c81, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1381a97c, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [],
      []>,

    InstrItinData <tc_15fdf750, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_16ff9ef8, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_191381c1, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 7, 1, 2, 7],
      [Hex_FWD, HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_1ad8a370, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_1ba8a0cd, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_20a4bbec, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_257f6f7c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_26a377fe, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2c745bb8, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_2d4051cd, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 3, 7, 5, 2],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_2e8f5f6e, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 7, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_309dbb4f, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_3904b926, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3aacf4a8, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_3ad719fb, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3c56e5ce, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3ce09744, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_3e2aaafc, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_447d9895, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_453fe68d, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_46d6c3e0, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_51d0ecc3, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_52447ecc, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_540c3da3, /*SLOT0,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [4, 7, 1],
      [Hex_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_54a0dc47, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [3, 2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_561aaa58, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_56c4f9fe, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_56e64202, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_58d21193, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_5bf8afbb, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_61bf7c03, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_649072c2, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_660769f1, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_663c80a7, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_6942b6e0, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_6e7fa133, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_71646d06, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7177e272, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_718b5c53, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9],
      [HVX_FWD]>,

    InstrItinData <tc_7273323b, /*SLOT0,STORE,VA_DV*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_7417e785, /*SLOT0123,VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_SHIFT]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_767c4e9d, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_7e6a3e89, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_8772086c, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_87adc037, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_8e420e4d, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [7, 1, 2, 7, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_90bcc1db, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_933f2b39, /*SLOT23,4SLOT_MPY*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL_NOMEM]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_946013d8, /*SLOT0123,VP*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 5],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_9d1dc972, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_9f363d21, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_a02a10a8, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [2, 1, 2, 7],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_a0dbea28, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [3, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_a7e6707d, /*SLOT0,NOSLOT1,LOAD,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ab23f776, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_abe8c3b2, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_ac4046bc, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_af25efd9, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 2, 7, 7],
      [HVX_FWD, Hex_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b091f1c6, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 7, 5, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_b28e51aa, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [2],
      [Hex_FWD]>,

    InstrItinData <tc_b4416217, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7],
      [HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_b9db8205, /*SLOT01,LOAD*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD]>], [9, 3, 2, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c0749f3c, /*SLOT01,LOAD,VA*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 1, 2],
      [HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_c127de3a, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_c4edf264, /*SLOT23,VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1]>], [9, 2],
      [HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_c5dba46e, /*SLOT0,STORE,VA*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_c7039829, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_cd94bfe0, /*SLOT23,VS_VX*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1], 0>,
       InstrStage<1, [CVI_SHIFT, CVI_XLANE]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_d8287c14, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_db5555f3, /*SLOT0123,VA_DV*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_dd5b0695, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [2, 1, 2],
      [Hex_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_df80eeb0, /*SLOT0123,VP_VS*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_XLSHF]>], [9, 7, 5, 5],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e2d2e9e5, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [3, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e35c1e93, /*SLOT0123,VA*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [9, 9, 7, 7],
      [HVX_FWD, HVX_FWD, HVX_FWD, HVX_FWD]>,

    InstrItinData <tc_e3f68a46, /*SLOT0123,4SLOT*/
      [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_ALL]>], [3],
      [HVX_FWD]>,

    InstrItinData <tc_e675c45a, /*SLOT23,VX_DV*/
      [InstrStage<1, [SLOT2, SLOT3], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 7, 5, 2, 2],
      [HVX_FWD, HVX_FWD, HVX_FWD, Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e699ae41, /*SLOT01,ZW*/
      [InstrStage<1, [SLOT0, SLOT1], 0>,
       InstrStage<1, [CVI_ZW]>], [1, 2],
      [Hex_FWD, Hex_FWD]>,

    InstrItinData <tc_e8797b98, /*SLOT1,LOAD,VA*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY0, CVI_MPY1, CVI_SHIFT, CVI_XLANE]>], [1, 2, 7],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_e99d4c2e, /*SLOT0,STORE*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [CVI_ST]>], [3, 2, 1, 2, 5],
      [Hex_FWD, Hex_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_f1de44ef, /*SLOT2,VX_DV*/
      [InstrStage<1, [SLOT2], 0>,
       InstrStage<1, [CVI_MPY01]>], [9, 5, 2],
      [HVX_FWD, HVX_FWD, Hex_FWD]>,

    InstrItinData <tc_f21e8abb, /*SLOT0,NOSLOT1,STORE,VP*/
      [InstrStage<1, [SLOT0], 0>,
       InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_ST], 0>,
       InstrStage<1, [CVI_XLANE]>], [1, 2, 5],
      [Hex_FWD, Hex_FWD, HVX_FWD]>,

    InstrItinData <tc_fd7610da, /*SLOT1,LOAD,VA_DV*/
      [InstrStage<1, [SLOT1], 0>,
       InstrStage<1, [CVI_LD], 0>,
       InstrStage<1, [CVI_MPY01, CVI_XLSHF]>], [7, 1, 2, 7],
      [HVX_FWD, Hex_FWD, Hex_FWD, HVX_FWD]>
  ];
}
