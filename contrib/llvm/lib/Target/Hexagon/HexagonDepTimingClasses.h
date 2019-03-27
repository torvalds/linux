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


#ifndef TARGET_HEXAGON_HEXAGON_DEP_TIMING_CLASSES_H
#define TARGET_HEXAGON_HEXAGON_DEP_TIMING_CLASSES_H

#include "HexagonInstrInfo.h"

namespace llvm {

inline bool is_TC3x(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_05d3a09b:
  case Hexagon::Sched::tc_0d8f5752:
  case Hexagon::Sched::tc_13bfbcf9:
  case Hexagon::Sched::tc_174516e8:
  case Hexagon::Sched::tc_1a2fd869:
  case Hexagon::Sched::tc_1c4528a2:
  case Hexagon::Sched::tc_32779c6f:
  case Hexagon::Sched::tc_5b54b33f:
  case Hexagon::Sched::tc_6b25e783:
  case Hexagon::Sched::tc_76851da1:
  case Hexagon::Sched::tc_9debc299:
  case Hexagon::Sched::tc_a9d88b22:
  case Hexagon::Sched::tc_bafaade3:
  case Hexagon::Sched::tc_bcf98408:
  case Hexagon::Sched::tc_bdceeac1:
  case Hexagon::Sched::tc_c8ce0b5c:
  case Hexagon::Sched::tc_d1aa9eaa:
  case Hexagon::Sched::tc_d773585a:
  case Hexagon::Sched::tc_df3319ed:
    return true;
  default:
    return false;
  }
}

inline bool is_TC2early(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_b4407292:
  case Hexagon::Sched::tc_fc3999b4:
    return true;
  default:
    return false;
  }
}

inline bool is_TC4x(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_2f7c551d:
  case Hexagon::Sched::tc_2ff964b4:
  case Hexagon::Sched::tc_3a867367:
  case Hexagon::Sched::tc_3b470976:
  case Hexagon::Sched::tc_4560740b:
  case Hexagon::Sched::tc_a58fd5cc:
  case Hexagon::Sched::tc_b8bffe55:
    return true;
  default:
    return false;
  }
}

inline bool is_TC2(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_002cb246:
  case Hexagon::Sched::tc_14b5c689:
  case Hexagon::Sched::tc_1c80410a:
  case Hexagon::Sched::tc_4414d8b1:
  case Hexagon::Sched::tc_6132ba3d:
  case Hexagon::Sched::tc_61830035:
  case Hexagon::Sched::tc_679309b8:
  case Hexagon::Sched::tc_703e822c:
  case Hexagon::Sched::tc_779080bf:
  case Hexagon::Sched::tc_784490da:
  case Hexagon::Sched::tc_88b4f13d:
  case Hexagon::Sched::tc_9461ff31:
  case Hexagon::Sched::tc_9e313203:
  case Hexagon::Sched::tc_a813cf9a:
  case Hexagon::Sched::tc_bfec0f01:
  case Hexagon::Sched::tc_cf8126ae:
  case Hexagon::Sched::tc_d08ee0f4:
  case Hexagon::Sched::tc_e4a7f9f0:
  case Hexagon::Sched::tc_f429765c:
  case Hexagon::Sched::tc_f675fee8:
  case Hexagon::Sched::tc_f9058dd7:
    return true;
  default:
    return false;
  }
}

inline bool is_TC1(unsigned SchedClass) {
  switch (SchedClass) {
  case Hexagon::Sched::tc_0663f615:
  case Hexagon::Sched::tc_0a705168:
  case Hexagon::Sched::tc_0ae0825c:
  case Hexagon::Sched::tc_1b6f7cec:
  case Hexagon::Sched::tc_1fc97744:
  case Hexagon::Sched::tc_20cdee80:
  case Hexagon::Sched::tc_2332b92e:
  case Hexagon::Sched::tc_2eabeebe:
  case Hexagon::Sched::tc_3d495a39:
  case Hexagon::Sched::tc_4c5ba658:
  case Hexagon::Sched::tc_56336eb0:
  case Hexagon::Sched::tc_56f114f4:
  case Hexagon::Sched::tc_57890846:
  case Hexagon::Sched::tc_5a2711e5:
  case Hexagon::Sched::tc_5b7c0967:
  case Hexagon::Sched::tc_640086b5:
  case Hexagon::Sched::tc_643b4717:
  case Hexagon::Sched::tc_85c9c08f:
  case Hexagon::Sched::tc_85d5d03f:
  case Hexagon::Sched::tc_862b3e70:
  case Hexagon::Sched::tc_946df596:
  case Hexagon::Sched::tc_9c3ecd83:
  case Hexagon::Sched::tc_9fc3dae0:
  case Hexagon::Sched::tc_a1123dda:
  case Hexagon::Sched::tc_a1c00888:
  case Hexagon::Sched::tc_ae53734a:
  case Hexagon::Sched::tc_b31c2e97:
  case Hexagon::Sched::tc_b4b5c03a:
  case Hexagon::Sched::tc_b51dc29a:
  case Hexagon::Sched::tc_cd374165:
  case Hexagon::Sched::tc_cfd8378a:
  case Hexagon::Sched::tc_d5b7b0c1:
  case Hexagon::Sched::tc_d9d43ecb:
  case Hexagon::Sched::tc_db2bce9c:
  case Hexagon::Sched::tc_de4df740:
  case Hexagon::Sched::tc_de554571:
  case Hexagon::Sched::tc_e78647bd:
    return true;
  default:
    return false;
  }
}
} // namespace llvm

#endif