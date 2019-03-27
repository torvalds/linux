//=-HexagonScheduleV5.td - HexagonV5 Scheduling Definitions --*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

def LD_tc_ld_SLOT01 : InstrItinClass;
def ST_tc_st_SLOT01 : InstrItinClass;

class HexagonV5PseudoItin {
  list<InstrItinData> V5PseudoItin_list = [
    InstrItinData<PSEUDO,     [InstrStage<1, [SLOT0, SLOT1, SLOT2, SLOT3]>]>,
    InstrItinData<PSEUDOM,    [InstrStage<1, [SLOT2, SLOT3], 0>,
                               InstrStage<1, [SLOT2, SLOT3]>]>,
    InstrItinData<DUPLEX,     [InstrStage<1, [SLOT0]>]>,
    InstrItinData<tc_ENDLOOP, [InstrStage<1, [SLOT_ENDLOOP]>]>
  ];
}

def HexagonV5ItinList : DepScalarItinV5, HexagonV5PseudoItin {
  list<InstrItinData> V5Itin_list = [
    InstrItinData<LD_tc_ld_SLOT01, [InstrStage<1, [SLOT0, SLOT1]>]>,
    InstrItinData<ST_tc_st_SLOT01, [InstrStage<1, [SLOT0, SLOT1]>]>
  ];
  list<InstrItinData> ItinList =
    !listconcat(V5Itin_list, DepScalarItinV5_list, V5PseudoItin_list);
}

def HexagonItinerariesV5 :
      ProcessorItineraries<[SLOT0, SLOT1, SLOT2, SLOT3, SLOT_ENDLOOP],
                           [Hex_FWD], HexagonV5ItinList.ItinList>;

def HexagonModelV5 : SchedMachineModel {
  // Max issue per cycle == bundle width.
  let IssueWidth = 4;
  let Itineraries = HexagonItinerariesV5;
  let LoadLatency = 1;
  let CompleteModel = 0;
}

//===----------------------------------------------------------------------===//
// Hexagon V5 Resource Definitions -
//===----------------------------------------------------------------------===//
