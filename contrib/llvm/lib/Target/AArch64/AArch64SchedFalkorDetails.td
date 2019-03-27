//==- AArch64SchedFalkorDetails.td - Falkor Scheduling Defs -*- tablegen -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the uop and latency details for the machine model for the
// Qualcomm Falkor subtarget.
//
//===----------------------------------------------------------------------===//

// Contains all of the Falkor specific SchedWriteRes types. The approach
// below is to define a generic SchedWriteRes for every combination of
// latency and microOps. The naming conventions is to use a prefix, one field
// for latency, and one or more microOp count/type designators.
//   Prefix: FalkorWr
//   MicroOp Count/Types: #(B|X|Y|Z|LD|ST|SD|VX|VY|VSD)
//   Latency: #cyc
//
// e.g. FalkorWr_1Z_6SD_4VX_6cyc means there are 11 micro-ops to be issued
//      down one Z pipe, six SD pipes, four VX pipes and the total latency is
//      six cycles.
//
// Contains all of the Falkor specific ReadAdvance types for forwarding logic.
//
// Contains all of the Falkor specific WriteVariant types for immediate zero
// and LSLFast.
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Define 0 micro-op types
def FalkorWr_LdInc_none_2cyc : SchedWriteRes<[]> {
  let Latency = 2;
  let NumMicroOps = 0;
}
def FalkorWr_StInc_none_2cyc : SchedWriteRes<[]> {
  let Latency = 2;
  let NumMicroOps = 0;
}
def FalkorWr_none_3cyc : SchedWriteRes<[]> {
  let Latency = 3;
  let NumMicroOps = 0;
}
def FalkorWr_none_4cyc : SchedWriteRes<[]> {
  let Latency = 4;
  let NumMicroOps = 0;
}

//===----------------------------------------------------------------------===//
// Define 1 micro-op types

def FalkorWr_1X_2cyc    : SchedWriteRes<[FalkorUnitX]>   { let Latency = 2; }
def FalkorWr_IMUL32_1X_2cyc : SchedWriteRes<[FalkorUnitX]> { let Latency = 4; }
def FalkorWr_IMUL64_1X_4cyc : SchedWriteRes<[FalkorUnitX]> { let Latency = 4; }
def FalkorWr_IMUL64_1X_5cyc : SchedWriteRes<[FalkorUnitX]> { let Latency = 5; }
def FalkorWr_1Z_0cyc    : SchedWriteRes<[FalkorUnitZ]>   { let Latency = 0; }
def FalkorWr_1ZB_0cyc   : SchedWriteRes<[FalkorUnitZB]>  { let Latency = 0; }
def FalkorWr_1LD_3cyc   : SchedWriteRes<[FalkorUnitLD]>  { let Latency = 3; }
def FalkorWr_1LD_4cyc   : SchedWriteRes<[FalkorUnitLD]>  { let Latency = 4; }
def FalkorWr_1XYZ_0cyc  : SchedWriteRes<[FalkorUnitXYZ]> { let Latency = 0; }
def FalkorWr_1XYZ_1cyc  : SchedWriteRes<[FalkorUnitXYZ]> { let Latency = 1; }
def FalkorWr_1XYZ_2cyc  : SchedWriteRes<[FalkorUnitXYZ]> { let Latency = 2; }
def FalkorWr_1XYZB_0cyc : SchedWriteRes<[FalkorUnitXYZB]>{ let Latency = 0; }
def FalkorWr_1XYZB_1cyc : SchedWriteRes<[FalkorUnitXYZB]>{ let Latency = 1; }
def FalkorWr_1none_0cyc : SchedWriteRes<[]>              { let Latency = 0; }

def FalkorWr_1VXVY_0cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 0; }
def FalkorWr_1VXVY_1cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 1; }
def FalkorWr_1VXVY_2cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 2; }
def FalkorWr_1VXVY_3cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 3; }
def FalkorWr_1VXVY_4cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 4; }
def FalkorWr_VMUL32_1VXVY_4cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 4; }
def FalkorWr_1VXVY_5cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 5; }
def FalkorWr_FMUL32_1VXVY_5cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 5; }
def FalkorWr_1VXVY_6cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 6; }
def FalkorWr_FMUL64_1VXVY_6cyc : SchedWriteRes<[FalkorUnitVXVY]>{ let Latency = 6; }

def FalkorWr_1LD_0cyc   : SchedWriteRes<[FalkorUnitLD]>  { let Latency = 0; }
def FalkorWr_1ST_0cyc   : SchedWriteRes<[FalkorUnitST]>  { let Latency = 0; }
def FalkorWr_1ST_3cyc   : SchedWriteRes<[FalkorUnitST]>  { let Latency = 3; }

def FalkorWr_1GTOV_0cyc : SchedWriteRes<[FalkorUnitGTOV]>{ let Latency = 0; }
def FalkorWr_1GTOV_1cyc : SchedWriteRes<[FalkorUnitGTOV]>{ let Latency = 1; }
def FalkorWr_1GTOV_4cyc : SchedWriteRes<[FalkorUnitGTOV]>{ let Latency = 4; }
def FalkorWr_1VTOG_1cyc : SchedWriteRes<[FalkorUnitVTOG]>{ let Latency = 1; }

//===----------------------------------------------------------------------===//
// Define 2 micro-op types

def FalkorWr_2VXVY_0cyc   : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 0;
  let NumMicroOps = 2;
}
def FalkorWr_2VXVY_1cyc   : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 1;
  let NumMicroOps = 2;
}
def FalkorWr_2VXVY_2cyc   : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 2;
  let NumMicroOps = 2;
}
def FalkorWr_2VXVY_3cyc   : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 3;
  let NumMicroOps = 2;
}
def FalkorWr_2VXVY_4cyc   : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def FalkorWr_VMUL32_2VXVY_4cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def FalkorWr_2VXVY_5cyc   : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 5;
  let NumMicroOps = 2;
}
def FalkorWr_FMUL32_2VXVY_5cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 5;
  let NumMicroOps = 2;
}
def FalkorWr_2VXVY_6cyc   : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 6;
  let NumMicroOps = 2;
}
def FalkorWr_FMUL64_2VXVY_6cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 6;
  let NumMicroOps = 2;
}

def FalkorWr_1LD_1VXVY_4cyc : SchedWriteRes<[FalkorUnitLD, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def FalkorWr_1XYZ_1LD_4cyc  : SchedWriteRes<[FalkorUnitXYZ, FalkorUnitLD]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def FalkorWr_2LD_3cyc   : SchedWriteRes<[FalkorUnitLD, FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

def FalkorWr_1VX_1VY_5cyc : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def FalkorWr_1VX_1VY_2cyc : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def FalkorWr_1VX_1VY_4cyc : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 4;
  let NumMicroOps = 2;
}

def FalkorWr_1VX_1VY_10cyc : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 10;
  let NumMicroOps = 2;
}

def FalkorWr_1VX_1VY_12cyc : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 12;
  let NumMicroOps = 2;
}

def FalkorWr_1VX_1VY_14cyc : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 14;
  let NumMicroOps = 2;
}

def FalkorWr_1VX_1VY_21cyc : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 21;
  let NumMicroOps = 2;
}

def FalkorWr_1GTOV_1VXVY_2cyc : SchedWriteRes<[FalkorUnitGTOV, FalkorUnitVXVY]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def FalkorWr_2GTOV_1cyc    : SchedWriteRes<[FalkorUnitGTOV, FalkorUnitGTOV]> {
  let Latency = 1;
  let NumMicroOps = 2;
}

def FalkorWr_1XYZ_1ST_4cyc: SchedWriteRes<[FalkorUnitXYZ, FalkorUnitST]> {
  let Latency = 4;
  let NumMicroOps = 2;
}
def FalkorWr_1XYZ_1LD_5cyc: SchedWriteRes<[FalkorUnitXYZ, FalkorUnitLD]> {
  let Latency = 5;
  let NumMicroOps = 2;
}

def FalkorWr_2XYZ_2cyc   : SchedWriteRes<[FalkorUnitXYZ, FalkorUnitXYZ]> {
  let Latency = 2;
  let NumMicroOps = 2;
}

def FalkorWr_1Z_1XY_0cyc : SchedWriteRes<[FalkorUnitZ, FalkorUnitXY]> {
  let Latency = 0;
  let NumMicroOps = 2;
}

def FalkorWr_1X_1Z_8cyc  : SchedWriteRes<[FalkorUnitX, FalkorUnitZ]> {
  let Latency = 8;
  let NumMicroOps = 2;
  let ResourceCycles = [2, 8];
}

def FalkorWr_1X_1Z_11cyc : SchedWriteRes<[FalkorUnitX, FalkorUnitZ]> {
  let Latency = 11;
  let NumMicroOps = 2;
  let ResourceCycles = [2, 11];
}

def FalkorWr_1LD_1Z_3cyc : SchedWriteRes<[FalkorUnitLD, FalkorUnitZ]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

def FalkorWr_1LD_1none_3cyc : SchedWriteRes<[FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 2;
}

def FalkorWr_1SD_1ST_0cyc: SchedWriteRes<[FalkorUnitSD, FalkorUnitST]> {
  let Latency = 0;
  let NumMicroOps = 2;
}

def FalkorWr_1VSD_1ST_0cyc: SchedWriteRes<[FalkorUnitVSD, FalkorUnitST]> {
  let Latency = 0;
  let NumMicroOps = 2;
}

//===----------------------------------------------------------------------===//
// Define 3 micro-op types

def FalkorWr_1ST_1SD_1LD_0cyc : SchedWriteRes<[FalkorUnitST, FalkorUnitSD,
                                               FalkorUnitLD]> {
  let Latency = 0;
  let NumMicroOps = 3;
}

def FalkorWr_1ST_1SD_1LD_3cyc : SchedWriteRes<[FalkorUnitST, FalkorUnitSD,
                                               FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 3;
}

def FalkorWr_3VXVY_3cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 3;
  let NumMicroOps = 3;
}

def FalkorWr_3VXVY_4cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

def FalkorWr_3VXVY_5cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 5;
  let NumMicroOps = 3;
}

def FalkorWr_3VXVY_6cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 6;
  let NumMicroOps = 3;
}

def FalkorWr_1LD_2VXVY_4cyc  : SchedWriteRes<[FalkorUnitLD, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 3;
}

def FalkorWr_2LD_1none_3cyc  : SchedWriteRes<[FalkorUnitLD, FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 3;
}

def FalkorWr_3LD_3cyc        : SchedWriteRes<[FalkorUnitLD, FalkorUnitLD,
                                              FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 3;
}

def FalkorWr_2LD_1Z_3cyc     : SchedWriteRes<[FalkorUnitLD, FalkorUnitLD,
                                             FalkorUnitZ]> {
  let Latency = 3;
  let NumMicroOps = 3;
}

def FalkorWr_1XYZ_1SD_1ST_0cyc: SchedWriteRes<[FalkorUnitXYZ, FalkorUnitSD, FalkorUnitST]> {
  let Latency = 0;
  let NumMicroOps = 3;
}
def FalkorWr_1XYZ_1VSD_1ST_0cyc: SchedWriteRes<[FalkorUnitXYZ, FalkorUnitVSD, FalkorUnitST]> {
  let Latency = 0;
  let NumMicroOps = 3;
}
//===----------------------------------------------------------------------===//
// Define 4 micro-op types

def FalkorWr_2VX_2VY_14cyc  : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY,
                                             FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 14;
  let NumMicroOps = 4;
}

def FalkorWr_2VX_2VY_20cyc  : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY,
                                             FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 20;
  let NumMicroOps = 4;
}

def FalkorWr_2VX_2VY_21cyc  : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY,
                                             FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 21;
  let NumMicroOps = 4;
}

def FalkorWr_2VX_2VY_24cyc  : SchedWriteRes<[FalkorUnitVX, FalkorUnitVY,
                                             FalkorUnitVX, FalkorUnitVY]> {
  let Latency = 24;
  let NumMicroOps = 4;
}

def FalkorWr_4VXVY_2cyc    : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY,
                                            FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 2;
  let NumMicroOps = 4;
}
def FalkorWr_4VXVY_3cyc    : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY,
                                            FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 3;
  let NumMicroOps = 4;
}
def FalkorWr_4VXVY_4cyc    : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY,
                                            FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 4;
}
def FalkorWr_4VXVY_6cyc    : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY,
                                            FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 6;
  let NumMicroOps = 4;
}

def FalkorWr_4LD_3cyc      : SchedWriteRes<[FalkorUnitLD, FalkorUnitLD,
                                            FalkorUnitLD, FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 4;
}

def FalkorWr_1LD_3VXVY_4cyc: SchedWriteRes<[FalkorUnitLD, FalkorUnitVXVY,
                                            FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 4;
}

def FalkorWr_2LD_2none_3cyc: SchedWriteRes<[FalkorUnitLD, FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 4;
}

def FalkorWr_2LD_1ST_1SD_3cyc: SchedWriteRes<[FalkorUnitLD, FalkorUnitST,
                                              FalkorUnitSD, FalkorUnitLD]> {
  let Latency = 3;
  let NumMicroOps = 4;
}

def FalkorWr_2VSD_2ST_0cyc: SchedWriteRes<[FalkorUnitST, FalkorUnitVSD,
                                           FalkorUnitST, FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 4;
}

//===----------------------------------------------------------------------===//
// Define 5 micro-op types

def FalkorWr_1LD_4VXVY_4cyc: SchedWriteRes<[FalkorUnitLD, FalkorUnitVXVY,
                                            FalkorUnitVXVY, FalkorUnitVXVY,
                                            FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 5;
}
def FalkorWr_2LD_2VXVY_1none_4cyc: SchedWriteRes<[FalkorUnitLD, FalkorUnitLD,
                                            FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 5;
}
def FalkorWr_5VXVY_7cyc    : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitVXVY,
                                            FalkorUnitVXVY, FalkorUnitVXVY,
                                            FalkorUnitVXVY]> {
  let Latency = 7;
  let NumMicroOps = 5;
}
def FalkorWr_1XYZ_2ST_2VSD_0cyc: SchedWriteRes<[FalkorUnitXYZ, FalkorUnitST,
                                                FalkorUnitVSD, FalkorUnitST,
                                                FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 5;
}
def FalkorWr_1VXVY_2ST_2VSD_0cyc : SchedWriteRes<[FalkorUnitVXVY, FalkorUnitST,
                                                  FalkorUnitVSD, FalkorUnitST,
                                                  FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 5;
}
//===----------------------------------------------------------------------===//
// Define 6 micro-op types

def FalkorWr_2LD_2VXVY_2none_4cyc: SchedWriteRes<[FalkorUnitLD, FalkorUnitLD,
                                            FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 6;
}

def FalkorWr_2XYZ_2ST_2VSD_0cyc: SchedWriteRes<[FalkorUnitXYZ, FalkorUnitST,
                                                FalkorUnitVSD, FalkorUnitXYZ,
                                                FalkorUnitST, FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 6;
}

def FalkorWr_2VXVY_2ST_2VSD_0cyc: SchedWriteRes<[FalkorUnitVXVY, FalkorUnitST,
                                                 FalkorUnitVSD, FalkorUnitVXVY,
                                                 FalkorUnitST, FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 6;
}

def FalkorWr_3VSD_3ST_0cyc: SchedWriteRes<[FalkorUnitST, FalkorUnitVSD,
                                           FalkorUnitST, FalkorUnitVSD,
                                           FalkorUnitST, FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 6;
}

//===----------------------------------------------------------------------===//
// Define 8 micro-op types

def FalkorWr_2LD_2VXVY_2LD_2VXVY_4cyc:SchedWriteRes<[FalkorUnitLD, FalkorUnitLD,
                                             FalkorUnitVXVY, FalkorUnitVXVY,
                                             FalkorUnitLD, FalkorUnitLD,
                                             FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 8;
}

def FalkorWr_4VSD_4ST_0cyc: SchedWriteRes<[FalkorUnitST, FalkorUnitVSD,
                                           FalkorUnitST, FalkorUnitVSD,
                                           FalkorUnitST, FalkorUnitVSD,
                                           FalkorUnitST, FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 8;
}

//===----------------------------------------------------------------------===//
// Define 9 micro-op types

def FalkorWr_2LD_2VXVY_2LD_1XYZ_2VXVY_4cyc:SchedWriteRes<[FalkorUnitLD,
                                             FalkorUnitLD, FalkorUnitVXVY,
                                             FalkorUnitVXVY, FalkorUnitLD,
                                             FalkorUnitLD, FalkorUnitXYZ,
                                             FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 9;
}

def FalkorWr_2LD_2VXVY_1XYZ_2LD_2VXVY_4cyc:SchedWriteRes<[FalkorUnitLD,
                                             FalkorUnitLD, FalkorUnitVXVY,
                                             FalkorUnitVXVY, FalkorUnitXYZ,
                                             FalkorUnitLD, FalkorUnitLD,
                                             FalkorUnitVXVY, FalkorUnitVXVY]> {
  let Latency = 4;
  let NumMicroOps = 9;
}

//===----------------------------------------------------------------------===//
// Define 10 micro-op types

def FalkorWr_2VXVY_4ST_4VSD_0cyc: SchedWriteRes<[FalkorUnitVXVY, FalkorUnitST,
                                                 FalkorUnitVSD, FalkorUnitVXVY,
                                                 FalkorUnitST, FalkorUnitVSD,
                                                 FalkorUnitST, FalkorUnitVSD,
                                                 FalkorUnitST, FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 10;
}

//===----------------------------------------------------------------------===//
// Define 12 micro-op types

def FalkorWr_4VXVY_4ST_4VSD_0cyc: SchedWriteRes<[FalkorUnitVXVY, FalkorUnitST,
                                                 FalkorUnitVSD, FalkorUnitVXVY,
                                                 FalkorUnitST, FalkorUnitVSD,
                                                 FalkorUnitVXVY, FalkorUnitST,
                                                 FalkorUnitVSD, FalkorUnitVXVY,
                                                 FalkorUnitST, FalkorUnitVSD]> {
  let Latency = 0;
  let NumMicroOps = 12;
}

// Forwarding logic is modeled for multiply add/accumulate and
// load/store base register increment.
// -----------------------------------------------------------------------------
def FalkorReadIMA32  : SchedReadAdvance<3, [FalkorWr_IMUL32_1X_2cyc]>;
def FalkorReadIMA64  : SchedReadAdvance<4, [FalkorWr_IMUL64_1X_4cyc, FalkorWr_IMUL64_1X_5cyc]>;
def FalkorReadVMA    : SchedReadAdvance<3, [FalkorWr_VMUL32_1VXVY_4cyc, FalkorWr_VMUL32_2VXVY_4cyc]>;
def FalkorReadFMA32  : SchedReadAdvance<1, [FalkorWr_FMUL32_1VXVY_5cyc, FalkorWr_FMUL32_2VXVY_5cyc]>;
def FalkorReadFMA64  : SchedReadAdvance<2, [FalkorWr_FMUL64_1VXVY_6cyc, FalkorWr_FMUL64_2VXVY_6cyc]>;

def FalkorReadIncLd  : SchedReadAdvance<1, [FalkorWr_LdInc_none_2cyc]>;
def FalkorReadIncSt  : SchedReadAdvance<1, [FalkorWr_StInc_none_2cyc]>;

// SchedPredicates and WriteVariants for Immediate Zero and LSLFast/ASRFast
// -----------------------------------------------------------------------------
def FalkorImmZPred    : SchedPredicate<[{MI->getOperand(1).isImm() &&
                                         MI->getOperand(1).getImm() == 0}]>;
def FalkorOp1ZrReg    : SchedPredicate<[{MI->getOperand(1).getReg() == AArch64::WZR ||

                                         MI->getOperand(1).getReg() == AArch64::XZR}]>;
def FalkorShiftExtFastPred : SchedPredicate<[{TII->isFalkorShiftExtFast(*MI)}]>;

def FalkorWr_FMOV  : SchedWriteVariant<[
                       SchedVar<FalkorOp1ZrReg,  [FalkorWr_1none_0cyc]>,
                       SchedVar<NoSchedPred,     [FalkorWr_1GTOV_1cyc]>]>;

def FalkorWr_MOVZ  : SchedWriteVariant<[
                       SchedVar<FalkorImmZPred, [FalkorWr_1none_0cyc]>,
                       SchedVar<NoSchedPred,    [FalkorWr_1XYZB_0cyc]>]>; // imm fwd


def FalkorWr_ADDSUBsx : SchedWriteVariant<[
                          SchedVar<FalkorShiftExtFastPred, [FalkorWr_1XYZ_1cyc]>,
                          SchedVar<NoSchedPred,            [FalkorWr_2XYZ_2cyc]>]>;

def FalkorWr_LDRro : SchedWriteVariant<[
                       SchedVar<FalkorShiftExtFastPred, [FalkorWr_1LD_3cyc]>,
                       SchedVar<NoSchedPred,            [FalkorWr_1XYZ_1LD_4cyc]>]>;

def FalkorWr_LDRSro : SchedWriteVariant<[
                        SchedVar<FalkorShiftExtFastPred, [FalkorWr_1LD_4cyc]>,
                        SchedVar<NoSchedPred,            [FalkorWr_1XYZ_1LD_5cyc]>]>;

def FalkorWr_ORRi : SchedWriteVariant<[
                      SchedVar<FalkorOp1ZrReg, [FalkorWr_1XYZ_0cyc]>, // imm fwd
                      SchedVar<NoSchedPred,    [FalkorWr_1XYZ_1cyc]>]>;

def FalkorWr_PRFMro : SchedWriteVariant<[
                        SchedVar<FalkorShiftExtFastPred, [FalkorWr_1ST_3cyc]>,
                        SchedVar<NoSchedPred,            [FalkorWr_1XYZ_1ST_4cyc]>]>;

def FalkorWr_STRVro : SchedWriteVariant<[
                        SchedVar<FalkorShiftExtFastPred, [FalkorWr_1VSD_1ST_0cyc]>,
                        SchedVar<NoSchedPred,            [FalkorWr_1XYZ_1VSD_1ST_0cyc]>]>;

def FalkorWr_STRQro : SchedWriteVariant<[
                        SchedVar<FalkorShiftExtFastPred, [FalkorWr_1XYZ_2ST_2VSD_0cyc]>,
                        SchedVar<NoSchedPred,            [FalkorWr_2XYZ_2ST_2VSD_0cyc]>]>;

def FalkorWr_STRro : SchedWriteVariant<[
                       SchedVar<FalkorShiftExtFastPred, [FalkorWr_1SD_1ST_0cyc]>,
                       SchedVar<NoSchedPred,            [FalkorWr_1XYZ_1SD_1ST_0cyc]>]>;

//===----------------------------------------------------------------------===//
// Specialize the coarse model by associating instruction groups with the
// subtarget-defined types. As the modeled is refined, this will override most
// of the earlier mappings.

// Miscellaneous
// -----------------------------------------------------------------------------

// FIXME: This could be better modeled by looking at the regclasses of the operands.
def : InstRW<[FalkorWr_1XYZ_1cyc], (instrs COPY)>;

// SIMD Floating-point Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^(FABS|FNEG)v2f32$")>;

def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^(F(MAX|MIN)(NM)?P?|FAC(GE|GT))(v2f32|v2i32p)$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^FAC(GE|GT)(32|64)$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^FCM(EQ|GE|GT)(32|64|v2f32|v2i32)$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^FCM(EQ|LE|GE|GT|LT)(v1i32|v1i64|v2i32)rz$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^FRINT(A|I|M|N|P|X|Z)v2f32$")>;

def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^F(MAX|MIN)(NM)?Vv4i32v$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(FABD|FADD|FSUB)v2f32$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^FADDP(v2i32p|v2i64p|v2f32)$")>;

def : InstRW<[FalkorWr_1VXVY_4cyc],   (instregex "^FCVT(N|M|P|Z|A)(S|U)(v1i32|v1i64|v2f32)$")>;
def : InstRW<[FalkorWr_1VXVY_4cyc],   (instrs FCVTXNv1i64)>;
def : InstRW<[FalkorWr_1VXVY_4cyc],   (instregex "^FCVTZ(S|U)v2i32(_shift)?$")>;

def : InstRW<[FalkorWr_FMUL32_1VXVY_5cyc],
                                      (instregex "^(FMUL|FMULX)(v2f32|(v1i32_indexed|v2i32_indexed))$")>;
def : InstRW<[FalkorWr_FMUL32_1VXVY_5cyc],
                                      (instrs FMULX32)>;

def : InstRW<[FalkorWr_FMUL64_1VXVY_6cyc],
                                      (instregex "^(FMUL|FMULX)v1i64_indexed$")>;
def : InstRW<[FalkorWr_FMUL64_1VXVY_6cyc],
                                      (instrs FMULX64)>;

def : InstRW<[FalkorWr_2VXVY_1cyc],   (instregex "^(FABS|FNEG)(v2f64|v4f32)$")>;

def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(F(MAX|MIN)(NM)?P?|FAC(GE|GT)|FCM(EQ|GE|GT))(v2f64|v4f32|v2i64p)$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^FCM(EQ|LE|GE|GT|LT)(v2i64|v4i32)rz$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instrs FCVTLv4i16, FCVTLv2i32)>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^FRINT(A|I|M|N|P|X|Z)(v2f64|v4f32)$")>;

def : InstRW<[FalkorWr_1VX_1VY_10cyc],(instrs FDIVv2f32)>;
def : InstRW<[FalkorWr_1VX_1VY_12cyc],(instrs FSQRTv2f32)>;

def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(FABD|FADD(P)?|FSUB)(v2f64|v4f32)$")>;

def : InstRW<[FalkorWr_2VXVY_4cyc],   (instregex "^FCVT(N|M|P|Z|A)(S|U)(v2f64|v4f32)$")>;
def : InstRW<[FalkorWr_2VXVY_4cyc],   (instrs FCVTLv8i16, FCVTLv4i32)>;
def : InstRW<[FalkorWr_2VXVY_4cyc],   (instregex "^FCVTZ(S|U)(v2i64|v4i32)(_shift)?$")>;

def : InstRW<[FalkorWr_FMUL32_2VXVY_5cyc],
                                      (instregex "^(FMUL|FMULX)(v2f64|v4f32|v4i32_indexed)$")>;

def : InstRW<[FalkorWr_FMUL64_2VXVY_6cyc],
                                      (instregex "^(FMUL|FMULX)v2i64_indexed$")>;

def : InstRW<[FalkorWr_3VXVY_4cyc],   (instrs FCVTNv4i16, FCVTNv2i32, FCVTXNv2f32)>;
def : InstRW<[FalkorWr_3VXVY_5cyc],   (instrs FCVTNv8i16, FCVTNv4i32, FCVTXNv4f32)>;

def : InstRW<[FalkorWr_2VX_2VY_14cyc],(instrs FDIVv2f64)>;
def : InstRW<[FalkorWr_2VX_2VY_20cyc],(instrs FDIVv4f32)>;
def : InstRW<[FalkorWr_2VX_2VY_21cyc],(instrs FSQRTv2f64)>;
def : InstRW<[FalkorWr_2VX_2VY_24cyc],(instrs FSQRTv4f32)>;

def : InstRW<[FalkorWr_VMUL32_1VXVY_4cyc, FalkorReadVMA],
                                      (instregex "^ML(A|S)(v8i8|v4i16|v2i32)(_indexed)?$")>;
def : InstRW<[FalkorWr_VMUL32_2VXVY_4cyc, FalkorReadVMA],
                                      (instregex "^ML(A|S)(v16i8|v8i16|v4i32|v2i64)(_indexed)?$")>;

def : InstRW<[FalkorWr_FMUL32_1VXVY_5cyc, FalkorReadFMA32],
                                      (instregex "^FML(A|S)(v2f32|(v1i32_indexed|v2i32_indexed))$")>;
def : InstRW<[FalkorWr_FMUL64_1VXVY_6cyc, FalkorReadFMA64],
                                      (instregex "^FML(A|S)v1i64_indexed$")>;
def : InstRW<[FalkorWr_FMUL32_2VXVY_5cyc, FalkorReadFMA32],
                                      (instregex "^FML(A|S)(v4f32|v4i32_indexed)$")>;
def : InstRW<[FalkorWr_FMUL64_2VXVY_6cyc, FalkorReadFMA64],
                                      (instregex "^FML(A|S)(v2f64|v2i64_indexed)$")>;

// SIMD Integer Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^ADD(v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instrs ADDPv2i64p)>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^(AND|ORR|ORN|BIC|EOR)v8i8$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^(BIC|ORR)(v2i32|v4i16)$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^NEG(v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^SUB(v1i64|v2i32|v4i16|v8i8)$")>;

def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^(S|U)(ADDLP|HADD|HSUB|SHL)(v2i32|v4i16|v8i8)(_v.*)?$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^(S|U)SHLv1i64$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^(S|U)SHR(v2i32|v4i16|v8i8)_shift$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^(S|U)SHRd$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^((S|U)?(MAX|MIN)P?|ABS|ADDP|CM(EQ|GE|HS|GT|HI))(v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^CM(EQ|GE|HS|GT|HI)(v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^CM(EQ|LE|GE|GT|LT)(v1i64|v2i32|v4i16|v8i8)rz$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^CMTST(v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instrs PMULv8i8)>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^SHL(v2i32|v4i16|v8i8)_shift$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^SHLd$")>;

def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^SQNEG(v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)R?SRA(d|(v2i32|v4i16|v8i8)_shift)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)(ABD|ADALP)(v8i8|v4i16|v2i32)(_v.*)?$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)ADDLVv4i16v$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)QADD(v1i8|v1i16|v2i16|v1i32|v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)QSHLU?(d|s|h|b|(v8i8|v4i16|v2i32)_shift)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)(QSHL|RSHL|QRSHL)(v1i8|v1i16|v1i32|v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(SQR?SHRN|UQR?SHRN|SQR?SHRUN)(s|h|b)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)QSUB(v1i8|v1i16|v2i16|v1i32|v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)RHADD(v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)RSHR(v2i32|v4i16|v8i8)_shift$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)RSHRd$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^R?SHRN(v2i32|v4i16|v8i8)_shift$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(SU|US)QADD(v1i8|v1i16|v2i16|v1i32|v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(S|U)?(MAX|MIN)V(v4i16v|v4i32v)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instrs ADDVv4i16v)>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^S(L|R)I(d|(v8i8|v4i16|v2i32)_shift)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^SQABS(v1i8|v1i16|v1i32|v1i64|v2i32|v4i16|v8i8)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^SQNEG(v1i8|v1i16|v1i32|v1i64)$")>;

def : InstRW<[FalkorWr_1VXVY_4cyc],   (instregex "^(S|U)ADDLVv8i8v$")>;
def : InstRW<[FalkorWr_1VXVY_4cyc],   (instregex "^(S|U)?(MAX|MIN)V(v8i8v|v8i16v)$")>;
def : InstRW<[FalkorWr_1VXVY_4cyc],   (instrs ADDVv8i8v)>;
def : InstRW<[FalkorWr_VMUL32_1VXVY_4cyc],
                                      (instregex "^MUL(v2i32|v4i16|v8i8)(_indexed)?$")>;
def : InstRW<[FalkorWr_VMUL32_1VXVY_4cyc],
                                      (instregex "^SQR?DMULH(v8i8|v4i16|v1i32|v2i32|v1i16)(_indexed)?$")>;
def : InstRW<[FalkorWr_VMUL32_1VXVY_4cyc],
                                      (instregex "^SQDMULL(i16|i32)$")>;
def : InstRW<[FalkorWr_VMUL32_1VXVY_4cyc, FalkorReadVMA],
                                      (instregex "^SQRDML(A|S)H(i16|i32|v8i8|v4i16|v1i32|v2i32|v1i16)(_indexed)?$")>;

def : InstRW<[FalkorWr_1VXVY_5cyc],   (instregex "^(S|U)?(MAX|MIN)Vv16i8v$")>;

def : InstRW<[FalkorWr_2VXVY_3cyc],   (instrs ADDVv4i32v)>;

def : InstRW<[FalkorWr_2VXVY_4cyc],   (instrs ADDVv8i16v)>;
def : InstRW<[FalkorWr_2VXVY_4cyc],   (instregex "^(ADD|SUB)HNv.*$")>;
def : InstRW<[FalkorWr_2VXVY_4cyc],   (instregex "^(S|U)ABA(v2i32|v4i16|v8i8)$")>;

def : InstRW<[FalkorWr_2VXVY_5cyc],   (instrs ADDVv16i8v)>;

def : InstRW<[FalkorWr_2VXVY_6cyc],   (instregex "^(SQR?SHRN|UQR?SHRN|SQR?SHRUN)(v8i8|v16i8|v4i16|v8i16|v2i32|v4i32)_shift?$")>;
def : InstRW<[FalkorWr_2VXVY_6cyc],   (instregex "^R(ADD|SUB)HNv.*$")>;

def : InstRW<[FalkorWr_2VXVY_1cyc],   (instregex "^ADD(v16i8|v8i16|v4i32|v2i64)$")>;
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instrs ADDPv2i64)>; // sz==11
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instregex "^(AND|ORR|ORN|BIC|EOR)v16i8$")>;
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instregex "^(BIC|ORR)(v8i16|v4i32)$")>;
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instregex "^(NEG|SUB)(v16i8|v8i16|v4i32|v2i64)$")>;

def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(S|U)ADDLv.*$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(S|U)(ADDLP|HADD|HSUB|SHL)(v16i8|v2i64|v4i32|v8i16)(_v.*)?$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(S|U)SHLL(v16i8|v8i16|v4i32|v8i8|v4i16|v2i32)(_shift)?$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(S|U)SHR(v16i8|v8i16|v4i32|v2i64)_shift$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(S|U)SUBLv.*$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^((S|U)?(MAX|MIN)P?|ABS)(v16i8|v2i64|v4i32|v8i16)$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^ADDP(v4i32|v8i16|v16i8)$")>; // sz!=11
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^CM(EQ|GE|HS|GT|HI)(v16i8|v2i64|v4i32|v8i16)$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^CM(EQ|LE|GE|GT|LT)(v16i8|v2i64|v4i32|v8i16)rz$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(CMTST|PMUL)(v16i8|v2i64|v4i32|v8i16)$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^PMULL(v8i8|v16i8)$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^SHL(v16i8|v8i16|v4i32|v2i64)_shift$")>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^SHLL(v16i8|v8i16|v4i32|v8i8|v4i16|v2i32)(_shift)?$")>;

def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(S|U)R?SRA(v2i64|v4i32|v8i16|v16i8)_shift$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(S|U)ABD(v16i8|v8i16|v4i32|v2i64)$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(S|U)ABDLv.*$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(S|U)(ADALP|QADD)(v16i8|v8i16|v4i32|v2i64)(_v.*)?$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(S|U)QSHLU?(v2i64|v4i32|v8i16|v16i8)_shift$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(S|U)(QSHL|RSHL|QRSHL|QSUB|RHADD)(v16i8|v8i16|v4i32|v2i64)$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(S|U)RSHR(v2i64|v4i32|v8i16|v16i8)_shift$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^R?SHRN(v2i64|v4i32|v8i16|v16i8)_shift$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^(SU|US)QADD(v16i8|v8i16|v4i32|v2i64)$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^PMULL(v1i64|v2i64)$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^S(L|R)I(v16i8|v8i16|v4i32|v2i64)_shift$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instregex "^SQ(ABS|NEG)(v16i8|v8i16|v4i32|v2i64)$")>;

def : InstRW<[FalkorWr_VMUL32_2VXVY_4cyc],
                                      (instregex "^(MUL|SQR?DMULH)(v16i8|v8i16|v4i32)(_indexed)?$")>;
def : InstRW<[FalkorWr_VMUL32_2VXVY_4cyc],
                                      (instregex "^SQDMULLv.*$")>;
def : InstRW<[FalkorWr_VMUL32_2VXVY_4cyc, FalkorReadVMA],
                                      (instregex "^SQRDML(A|S)H(v16i8|v8i16|v4i32)(_indexed)?$")>;

def : InstRW<[FalkorWr_3VXVY_3cyc],   (instregex "^(S|U)ADDLVv4i32v$")>;

def : InstRW<[FalkorWr_3VXVY_5cyc],   (instregex "^(S|U)ADDLVv8i16v$")>;

def : InstRW<[FalkorWr_3VXVY_6cyc],   (instregex "^(S|U)ADDLVv16i8v$")>;

def : InstRW<[FalkorWr_4VXVY_2cyc],   (instregex "^(S|U)(ADD|SUB)Wv.*$")>;

def : InstRW<[FalkorWr_4VXVY_3cyc],   (instregex "^(S|U)ABALv.*$")>;

def : InstRW<[FalkorWr_4VXVY_4cyc],   (instregex "^(S|U)ABA(v16i8|v8i16|v4i32)$")>;

def : InstRW<[FalkorWr_VMUL32_1VXVY_4cyc, FalkorReadVMA],
                                      (instregex "^SQD(MLAL|MLSL)(i16|i32|v1i32_indexed|v1i64_indexed)$")>;
def : InstRW<[FalkorWr_VMUL32_2VXVY_4cyc, FalkorReadVMA],
                                      (instregex "^SQD(MLAL|MLSL)v[248].*$")>;

// SIMD Load Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],       (instregex "^LD1(i64|Onev(8b|4h|2s|1d|16b|8h|4s|2d))$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1(i64|Onev(8b|4h|2s|1d|16b|8h|4s|2d))_POST$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],       (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1Rv(8b|4h|2s|1d|16b|8h|4s|2d)_POST$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],       (instrs LD2i64)>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                                         (instrs LD2i64_POST)>;

def : InstRW<[FalkorWr_1LD_1VXVY_4cyc, FalkorReadIncLd], (instregex "^LD1i(8|16|32)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_1VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD1i(8|16|32)_POST$")>;

def : InstRW<[FalkorWr_1LD_1none_3cyc, FalkorReadIncLd], (instregex "^LD1Twov(8b|4h|2s|1d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_1none_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1Twov(8b|4h|2s|1d)_POST$")>;
def : InstRW<[FalkorWr_1LD_1none_3cyc, FalkorReadIncLd], (instregex "^LD2Twov(8b|4h|2s)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_1none_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD2Twov(8b|4h|2s)_POST$")>;
def : InstRW<[FalkorWr_1LD_1none_3cyc, FalkorReadIncLd], (instregex "^LD2Rv(8b|4h|2s|1d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_1none_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD2Rv(8b|4h|2s|1d)_POST$")>;

def : InstRW<[FalkorWr_2LD_3cyc, FalkorReadIncLd],       (instregex "^LD1Twov(16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[FalkorWr_2LD_3cyc, FalkorReadIncLd],       (instregex "^LD2Twov(16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD2Twov(16b|8h|4s|2d)_POST$")>;
def : InstRW<[FalkorWr_2LD_3cyc, FalkorReadIncLd],       (instregex "^LD2Rv(16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD2Rv(16b|8h|4s|2d)_POST$")>;
def : InstRW<[FalkorWr_2LD_3cyc, FalkorReadIncLd],       (instrs LD3i64)>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_3cyc, FalkorReadIncLd],
                                                         (instrs LD3i64_POST)>;
def : InstRW<[FalkorWr_2LD_3cyc, FalkorReadIncLd],       (instrs LD4i64)>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_3cyc, FalkorReadIncLd],
                                                         (instrs LD4i64_POST)>;

def : InstRW<[FalkorWr_1LD_2VXVY_4cyc, FalkorReadIncLd], (instregex "^LD2i(8|16|32)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_2VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD2i(8|16|32)_POST$")>;

def : InstRW<[FalkorWr_2LD_1none_3cyc, FalkorReadIncLd], (instregex "^LD1Threev(8b|4h|2s|1d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_1none_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1Threev(8b|4h|2s|1d)_POST$")>;
def : InstRW<[FalkorWr_2LD_1none_3cyc, FalkorReadIncLd], (instregex "^LD3Rv(8b|4h|2s|1d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_1none_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD3Rv(8b|4h|2s|1d)_POST$")>;

def : InstRW<[FalkorWr_3LD_3cyc, FalkorReadIncLd],       (instregex "^LD1Threev(16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_3LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1Threev(16b|8h|4s|2d)_POST$")>;
def : InstRW<[FalkorWr_3LD_3cyc, FalkorReadIncLd],       (instrs LD3Threev2d)>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_3LD_3cyc, FalkorReadIncLd],
                                                         (instrs LD3Threev2d_POST)>;
def : InstRW<[FalkorWr_3LD_3cyc, FalkorReadIncLd],       (instregex "^LD3Rv(16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_3LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD3Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[FalkorWr_1LD_3VXVY_4cyc, FalkorReadIncLd], (instregex "^LD3i(8|16|32)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_3VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD3i(8|16|32)_POST$")>;

def : InstRW<[FalkorWr_2LD_2none_3cyc, FalkorReadIncLd], (instregex "^LD1Fourv(8b|4h|2s|1d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_2none_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1Fourv(8b|4h|2s|1d)_POST$")>;
def : InstRW<[FalkorWr_2LD_2none_3cyc, FalkorReadIncLd], (instregex "^LD4Rv(8b|4h|2s|1d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_2none_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD4Rv(8b|4h|2s|1d)_POST$")>;

def : InstRW<[FalkorWr_4LD_3cyc, FalkorReadIncLd],       (instregex "^LD1Fourv(16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_4LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD1Fourv(16b|8h|4s|2d)_POST$")>;
def : InstRW<[FalkorWr_4LD_3cyc, FalkorReadIncLd],       (instrs LD4Fourv2d)>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_4LD_3cyc, FalkorReadIncLd],
                                                         (instrs LD4Fourv2d_POST)>;
def : InstRW<[FalkorWr_4LD_3cyc, FalkorReadIncLd],       (instregex "^LD4Rv(16b|8h|4s|2d)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_4LD_3cyc, FalkorReadIncLd],
                                                         (instregex "^LD4Rv(16b|8h|4s|2d)_POST$")>;

def : InstRW<[FalkorWr_1LD_4VXVY_4cyc, FalkorReadIncLd], (instregex "^LD4i(8|16|32)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_4VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD4i(8|16|32)_POST$")>;

def : InstRW<[FalkorWr_2LD_2VXVY_1none_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD3Threev(8b|4h|2s)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_2VXVY_1none_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD3Threev(8b|4h|2s)_POST$")>;

def : InstRW<[FalkorWr_2LD_2VXVY_2none_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD4Fourv(8b|4h|2s)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_2VXVY_2none_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD4Fourv(8b|4h|2s)_POST$")>;

def : InstRW<[FalkorWr_2LD_2VXVY_2LD_2VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD3Threev(16b|8h|4s)$")>;

def : InstRW<[FalkorWr_2LD_2VXVY_2LD_2VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD4Fourv(16b|8h|4s)$")>;

def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_2VXVY_1XYZ_2LD_2VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD3Threev(16b|8h|4s)_POST$")>;

def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_2VXVY_2LD_1XYZ_2VXVY_4cyc, FalkorReadIncLd],
                                                         (instregex "^LD4Fourv(16b|8h|4s)_POST$")>;

// Arithmetic and Logical Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^(CCMN|CCMP)(W|X)(r|i)$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^ADC(S)?(W|X)r$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^ADD(S)?(W|X)r(r|i)$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^(CSEL|CSINC|CSINV|CSNEG)(W|X)r$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^AND(S)?(W|X)r(i|r|s)$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^BIC(S)?(W|X)r(r|s)$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^EON(W|X)r(r|s)$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^EOR(W|X)r(i|r|s)$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^ORN(W|X)r(r|s)$")>;
def : InstRW<[FalkorWr_ORRi],         (instregex "^ORR(W|X)ri$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^ORR(W|X)r(r|s)$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^SBC(S)?(W|X)r$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^SUB(S)?(W|X)r(r|i)$")>;
def : InstRW<[FalkorWr_ADDSUBsx],     (instregex "^ADD(S)?(W|X)r(s|x|x64)$")>;
def : InstRW<[FalkorWr_ADDSUBsx],     (instregex "^SUB(S)?(W|X)r(s|x|x64)$")>;

// SIMD Miscellaneous Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1GTOV_1cyc],   (instregex "^DUP(v8i8|v4i16|v2i32)(gpr|lane)$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^DUP(v16i8|v8i16)(gpr|lane)$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^CPY(i8|i16|i32|i64)$")>;
def : InstRW<[FalkorWr_1GTOV_1cyc],   (instregex "^INSv(i8|i16)(gpr|lane)$")>;
def : InstRW<[FalkorWr_1VTOG_1cyc],   (instregex "^(S|U)MOVv.*$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^(BIF|BIT|BSL)v8i8$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instrs EXTv8i8)>;
def : InstRW<[FalkorWr_1VXVY_0cyc],   (instregex "(MOVI|MVNI)(D|v8b_ns|v2i32|v4i16|v2s_msl)$")>; // imm fwd
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instrs TBLv8i8One)>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instrs NOTv8i8)>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^REV(16|32|64)v.*$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^(TRN1|TRN2|ZIP1|UZP1|UZP2|ZIP2|XTN)(v2i32|v2i64|v4i16|v4i32|v8i8|v8i16|v16i8)$")>;

def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^(CLS|CLZ|CNT|RBIT)(v2i32|v4i16|v8i8)$")>;

def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "(S|U)QXTU?Nv.*$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instrs FRECPEv1i32, FRECPEv1i64, FRSQRTEv1i32, FRSQRTEv1i64, FRECPEv2f32, FRSQRTEv2f32)>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instrs FRECPXv1i32, FRECPXv1i64)>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instrs URECPEv2i32, URSQRTEv2i32)>;

def : InstRW<[FalkorWr_FMUL32_1VXVY_5cyc],
                                      (instrs FRECPS32, FRSQRTS32, FRECPSv2f32, FRSQRTSv2f32)>;

def : InstRW<[FalkorWr_FMUL64_1VXVY_6cyc],
                                      (instrs FRECPS64, FRSQRTS64)>;

def : InstRW<[FalkorWr_1GTOV_1VXVY_2cyc],
                                      (instregex "^INSv(i32|i64)(gpr|lane)$")>;
def : InstRW<[FalkorWr_2GTOV_1cyc],   (instregex "^DUP(v4i32|v2i64)(gpr|lane)$")>;
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instregex "^(BIF|BIT|BSL)v16i8$")>;
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instrs EXTv16i8)>;
def : InstRW<[FalkorWr_2VXVY_0cyc],   (instregex "(MOVI|MVNI)(v2d_ns|v16b_ns|v4i32|v8i16|v4s_msl)$")>; // imm fwd
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instrs NOTv16i8)>;
def : InstRW<[FalkorWr_2VXVY_1cyc],   (instrs TBLv16i8One)>;

def : InstRW<[FalkorWr_2VXVY_2cyc],   (instregex "^(CLS|CLZ|CNT|RBIT)(v4i32|v8i16|v16i8)$")>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instrs FRECPEv2f64, FRECPEv4f32, FRSQRTEv2f64, FRSQRTEv4f32)>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instrs URECPEv4i32, URSQRTEv4i32)>;

def : InstRW<[FalkorWr_2VXVY_4cyc],   (instrs TBLv8i8Two)>;
def : InstRW<[FalkorWr_2VXVY_4cyc],   (instregex "^TBX(v8|v16)i8One$")>;

def : InstRW<[FalkorWr_FMUL32_2VXVY_5cyc],
                                      (instrs FRECPSv4f32, FRSQRTSv4f32)>;

def : InstRW<[FalkorWr_FMUL64_2VXVY_6cyc],
                                      (instrs FRECPSv2f64, FRSQRTSv2f64)>;

def : InstRW<[FalkorWr_3VXVY_5cyc],   (instregex "^TBL(v8i8Three|v16i8Two)$")>;
def : InstRW<[FalkorWr_3VXVY_5cyc],   (instregex "^TBX(v8i8Two|v16i8Two)$")>;

def : InstRW<[FalkorWr_4VXVY_6cyc],   (instregex "^TBL(v8i8Four|v16i8Three)$")>;
def : InstRW<[FalkorWr_4VXVY_6cyc],   (instregex "^TBX(v8i8Three|v16i8Three)$")>;

def : InstRW<[FalkorWr_5VXVY_7cyc],   (instrs TBLv16i8Four)>;
def : InstRW<[FalkorWr_5VXVY_7cyc],   (instregex "^TBX(v8i8Four|v16i8Four)$")>;

// SIMD Store Instructions
// -----------------------------------------------------------------------------

def : InstRW<[FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STR(Q|D|S|H|B)ui$")>;
def : InstRW<[FalkorWr_StInc_none_2cyc, FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STR(Q|D|S|H|B)(post|pre)$")>;
def : InstRW<[FalkorWr_STRVro, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STR(D|S|H|B)ro(W|X)$")>;
def : InstRW<[FalkorWr_2VSD_2ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STPQi$")>;
def : InstRW<[FalkorWr_StInc_none_2cyc, FalkorWr_2VSD_2ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STPQ(post|pre)$")>;
def : InstRW<[FalkorWr_1VSD_1ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STP(D|S)(i)$")>;
def : InstRW<[FalkorWr_StInc_none_2cyc, FalkorWr_1VSD_1ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STP(D|S)(post|pre)$")>;
def : InstRW<[FalkorWr_STRQro, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STRQro(W|X)$")>;
def : InstRW<[FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^STUR(Q|D|S|B|H)i$")>;
def : InstRW<[FalkorWr_1VSD_1ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                       (instrs STNPDi, STNPSi)>;
def : InstRW<[FalkorWr_2VSD_2ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                       (instrs STNPQi)>;

def : InstRW<[FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1(One(v8b|v4h|v2s|v1d)|(i8|i16|i32|i64)|One(v16b|v8h|v4s|v2d)|Two(v8b|v4h|v2s|v1d))$")>;
def : InstRW<[FalkorWr_StInc_none_2cyc, FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1(One(v8b|v4h|v2s|v1d)_POST|(i8|i16|i32|i64)_POST)$")>;
def : InstRW<[FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST2(Two(v8b|v4h|v2s)|(i8|i16|i32|i64))$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1(One(v16b|v8h|v4s|v2d)|Two(v8b|v4h|v2s|v1d))_POST$")>;
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_1VSD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST2(Two(v8b|v4h|v2s)|(i8|i16|i32|i64))_POST$")>;

def : InstRW<[FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1(Two(v16b|v8h|v4s|v2d)|(Three|Four)(v8b|v4h|v2s|v1d))$")>;
def : InstRW<[FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST2Two(v16b|v8h|v4s|v2d)$")>;
def : InstRW<[FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST3(i8|i16|i32|i64)$")>;
def : InstRW<[FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST4(i8|i16|i32|i64)$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1(Two(v16b|v8h|v4s|v2d)|(Three|Four)(v8b|v4h|v2s|v1d))_POST$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST2Two(v16b|v8h|v4s|v2d)_POST$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST3(i8|i16|i32|i64)_POST$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_2VSD_2ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST4(i8|i16|i32|i64)_POST$")>;

def : InstRW<[FalkorWr_1VXVY_2ST_2VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST3Three(v8b|v4h|v2s)$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_1VXVY_2ST_2VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST3Three(v8b|v4h|v2s)_POST$")>;

def : InstRW<[FalkorWr_3VSD_3ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1Three(v16b|v8h|v4s|v2d)$")>;
def : InstRW<[FalkorWr_3VSD_3ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instrs ST3Threev2d)>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_3VSD_3ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1Three(v16b|v8h|v4s|v2d)_POST$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_3VSD_3ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instrs ST3Threev2d_POST)>;

def : InstRW<[FalkorWr_2VXVY_2ST_2VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST4Four(v8b|v4h|v2s)$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_2VXVY_2ST_2VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST4Four(v8b|v4h|v2s)_POST$")>;

def : InstRW<[FalkorWr_4VSD_4ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1Four(v16b|v8h|v4s|v2d)$")>;
def : InstRW<[FalkorWr_4VSD_4ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instrs ST4Fourv2d)>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_4VSD_4ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST1Four(v16b|v8h|v4s|v2d)_POST$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_4VSD_4ST_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instrs ST4Fourv2d_POST)>;

def : InstRW<[FalkorWr_2VXVY_4ST_4VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST3Three(v16b|v8h|v4s)$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_2VXVY_4ST_4VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST3Three(v16b|v8h|v4s)_POST$")>;

def : InstRW<[FalkorWr_4VXVY_4ST_4VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST4Four(v16b|v8h|v4s)$")>;
// FIXME: This is overly conservative in the imm POST case (no XYZ used in that case).
def : InstRW<[FalkorWr_1XYZ_1cyc, FalkorWr_4VXVY_4ST_4VSD_0cyc, ReadDefault, FalkorReadIncSt],
                                       (instregex "^ST4Four(v16b|v8h|v4s)_POST$")>;

// Branch Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1none_0cyc],   (instrs B, TCRETURNdi)>;
def : InstRW<[FalkorWr_1Z_0cyc],      (instregex "^(BR|RET|(CBZ|CBNZ|TBZ|TBNZ)(W|X))$")>;
def : InstRW<[FalkorWr_1Z_0cyc],      (instrs RET_ReallyLR, TCRETURNri)>;
def : InstRW<[FalkorWr_1ZB_0cyc],     (instrs Bcc)>;
def : InstRW<[FalkorWr_1XYZB_0cyc],   (instrs BL)>;
def : InstRW<[FalkorWr_1Z_1XY_0cyc],  (instrs BLR)>;

// Cryptography Extensions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instrs SHA1Hrr)>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instrs AESIMCrr, AESMCrr)>;
def : InstRW<[FalkorWr_2VXVY_3cyc],   (instrs AESDrr, AESErr)>;
def : InstRW<[FalkorWr_2VXVY_2cyc],   (instrs SHA1SU0rrr, SHA1SU1rr, SHA256SU0rr)>;
def : InstRW<[FalkorWr_1VX_1VY_4cyc], (instregex "^SHA1(C|M|P)rrr$")>;
def : InstRW<[FalkorWr_1VX_1VY_5cyc], (instrs SHA256H2rrr, SHA256Hrrr)>;
def : InstRW<[FalkorWr_4VXVY_3cyc],   (instrs SHA256SU1rrr)>;

// FP Load Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDR((Q|D|S|H|B)ui|(Q|D|S)l)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDR(Q|D|S|H|B)(post|pre)$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDUR(Q|D|S|H|B)i$")>;
def : InstRW<[FalkorWr_LDRro, FalkorReadIncLd],
                                      (instregex "^LDR(Q|D|H|S|B)ro(W|X)$")>;
def : InstRW<[FalkorWr_2LD_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instrs LDNPQi)>;
def : InstRW<[FalkorWr_2LD_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instrs LDPQi)>;
def : InstRW<[FalkorWr_1LD_1none_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "LDNP(D|S)i$")>;
def : InstRW<[FalkorWr_1LD_1none_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "LDP(D|S)i$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_1none_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "LDP(D|S)(pre|post)$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_2LD_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "^LDPQ(pre|post)$")>;

// FP Data Processing Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^FCCMP(E)?(S|D)rr$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^FCMP(E)?(S|D)r(r|i)$")>;
def : InstRW<[FalkorWr_1VTOG_1cyc],   (instregex "^FCVT(A|M|N|P|Z)(S|U)U(W|X)(S|D)r$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^(FABS|FNEG)(S|D)r$")>;
def : InstRW<[FalkorWr_1VXVY_1cyc],   (instregex "^FCSEL(S|D)rrr$")>;

def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^F(MAX|MIN)(NM)?(S|D)rr$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^F(MAX|MIN)(NM)?Pv2i(32|64)p$")>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instrs FCVTSHr, FCVTDHr)>;
def : InstRW<[FalkorWr_1VXVY_2cyc],   (instregex "^FRINT(A|I|M|N|P|X|Z)(S|D)r$")>;

def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^FABD(32|64)$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instregex "^(FADD|FSUB)(S|D)rr$")>;
def : InstRW<[FalkorWr_1VXVY_3cyc],   (instrs FCVTHSr, FCVTHDr)>;

def : InstRW<[FalkorWr_1VXVY_4cyc],   (instrs FCVTSDr, FCVTDSr)>;

def : InstRW<[FalkorWr_FMUL32_1VXVY_5cyc],
                                      (instregex "^F(N)?MULSrr$")>;

def : InstRW<[FalkorWr_FMUL64_1VXVY_6cyc],
                                      (instregex "^F(N)?MULDrr$")>;

def : InstRW<[FalkorWr_1VX_1VY_10cyc],(instrs FDIVSrr)>;
def : InstRW<[FalkorWr_1VX_1VY_14cyc],(instrs FDIVDrr)>;
def : InstRW<[FalkorWr_1VX_1VY_12cyc],(instrs FSQRTSr)>;
def : InstRW<[FalkorWr_1VX_1VY_21cyc],(instrs FSQRTDr)>;

def : InstRW<[FalkorWr_FMUL32_1VXVY_5cyc, ReadDefault, ReadDefault, FalkorReadFMA32],
                                      (instregex "^F(N)?M(ADD|SUB)Srrr$")>;
def : InstRW<[FalkorWr_FMUL64_1VXVY_6cyc, ReadDefault, ReadDefault, FalkorReadFMA64],
                                      (instregex "^F(N)?M(ADD|SUB)Drrr$")>;

// FP Miscellaneous Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_FMOV],         (instregex "^FMOV(WS|XD|XDHigh)r$")>;
def : InstRW<[FalkorWr_1GTOV_0cyc],   (instregex "^FMOV(S|D)i$")>; // imm fwd
def : InstRW<[FalkorWr_1VTOG_1cyc],   (instregex "^FCVTZ(S|U)S(W|X)(D|S)ri$")>;
def : InstRW<[FalkorWr_1VTOG_1cyc],   (instregex "^FCVTZ(S|U)(d|s)$")>;
def : InstRW<[FalkorWr_1VTOG_1cyc],   (instregex "^FMOV(SW|DX|DXHigh)r$")>;
def : InstRW<[FalkorWr_1VXVY_0cyc],   (instregex "^FMOV(Sr|Dr|v.*_ns)$")>; // imm fwd
// FIXME: We are currently generating movi v0.2d, #0 for these, which is worse than fmov wzr/xzr
def : InstRW<[FalkorWr_2VXVY_0cyc],   (instrs FMOVD0, FMOVS0)>; // imm fwd

def : InstRW<[FalkorWr_1GTOV_4cyc],   (instregex "^(S|U)CVTF(S|U)(W|X)(D|S)ri$")>;
def : InstRW<[FalkorWr_1VXVY_4cyc],   (instregex "^(S|U)CVTF(v1i32|v2i32|v1i64|v2f32|d|s)(_shift)?")>;

def : InstRW<[FalkorWr_2VXVY_4cyc],   (instregex "^(S|U)CVTF(v2i64|v4i32|v2f64|v4f32)(_shift)?")>;

// Load Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1ST_0cyc],     (instrs PRFMui, PRFMl)>;
def : InstRW<[FalkorWr_1ST_0cyc],     (instrs PRFUMi)>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "^LDNP(W|X)i$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "^LDP(W|X)i$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "^LDP(W|X)(post|pre)$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDR(BB|HH|W|X)ui$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDR(BB|HH|W|X)(post|pre)$")>;
def : InstRW<[FalkorWr_LDRro, FalkorReadIncLd],
                                      (instregex "^LDR(BB|HH|W|X)ro(W|X)$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDR(W|X)l$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDTR(B|H|W|X)i$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^LDUR(BB|HH|W|X)i$")>;
def : InstRW<[FalkorWr_PRFMro],       (instregex "^PRFMro(W|X)$")>;
def : InstRW<[FalkorWr_1LD_4cyc, FalkorWr_none_4cyc, FalkorReadIncLd],
                                      (instrs LDPSWi)>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_4cyc, FalkorWr_none_4cyc, FalkorReadIncLd],
                                      (instregex "^LDPSW(post|pre)$")>;
def : InstRW<[FalkorWr_1LD_4cyc, FalkorReadIncLd],
                                      (instregex "^LDRS(BW|BX|HW|HX|W)ui$")>;
def : InstRW<[FalkorWr_LdInc_none_2cyc, FalkorWr_1LD_4cyc, FalkorReadIncLd],
                                      (instregex "^LDRS(BW|BX|HW|HX|W)(post|pre)$")>;
def : InstRW<[FalkorWr_LDRSro, FalkorReadIncLd],
                                      (instregex "^LDRS(BW|BX|HW|HX|W)ro(W|X)$")>;
def : InstRW<[FalkorWr_1LD_4cyc, FalkorReadIncLd],
                                      (instrs LDRSWl)>;
def : InstRW<[FalkorWr_1LD_4cyc, FalkorReadIncLd],
                                      (instregex "^LDTRS(BW|BX|HW|HX|W)i$")>;
def : InstRW<[FalkorWr_1LD_4cyc, FalkorReadIncLd],
                                      (instregex "^LDURS(BW|BX|HW|HX|W)i$")>;

// Miscellaneous Data-Processing Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^(S|U)?BFM(W|X)ri$")>;
def : InstRW<[FalkorWr_1X_2cyc],      (instregex "^CRC32.*$")>;
def : InstRW<[FalkorWr_1XYZ_2cyc],    (instregex "^(CLS|CLZ|RBIT|REV|REV16|REV32)(W|X)r$")>;
def : InstRW<[FalkorWr_2XYZ_2cyc],    (instregex "^EXTR(W|X)rri$")>;

// Divide and Multiply Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_IMUL64_1X_4cyc, ReadDefault, ReadDefault, FalkorReadIMA64],
                                        (instregex "^(S|U)M(ADD|SUB)Lrrr$")>;
def : InstRW<[FalkorWr_IMUL32_1X_2cyc, ReadDefault, ReadDefault, FalkorReadIMA32],
                                        (instregex "^M(ADD|SUB)Wrrr$")>;

def : InstRW<[FalkorWr_IMUL64_1X_5cyc], (instregex "^(S|U)MULHrr$")>;
def : InstRW<[FalkorWr_IMUL64_1X_5cyc, ReadDefault, ReadDefault, FalkorReadIMA64],
                                        (instregex "^M(ADD|SUB)Xrrr$")>;

def : InstRW<[FalkorWr_1X_1Z_8cyc],     (instregex "^(S|U)DIVWr$")>;
def : InstRW<[FalkorWr_1X_1Z_11cyc],    (instregex "^(S|U)DIVXr$")>;

def : InstRW<[FalkorWr_VMUL32_2VXVY_4cyc],
                                        (instregex "^(S|U)MULLv.*$")>;
def : InstRW<[FalkorWr_VMUL32_2VXVY_4cyc, FalkorReadVMA],
                                        (instregex "^(S|U)(MLAL|MLSL)v.*$")>;

// Move and Shift Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1XYZ_1cyc],    (instregex "^(LSLV|LSRV|ASRV|RORV)(W|X)r$")>;
def : InstRW<[FalkorWr_1XYZ_0cyc],    (instregex "^MOVK(W|X)i$")>; // imm fwd
def : InstRW<[FalkorWr_1XYZB_0cyc],   (instregex "^ADRP?$")>; // imm fwd
def : InstRW<[FalkorWr_1XYZB_0cyc],   (instregex "^MOVN(W|X)i$")>; // imm fwd
def : InstRW<[FalkorWr_MOVZ],         (instregex "^MOVZ(W|X)i$")>;
def : InstRW<[FalkorWr_1XYZ_0cyc],    (instrs MOVi32imm, MOVi64imm)>; // imm fwd (approximation)
def : InstRW<[WriteSequence<[FalkorWr_1XYZ_1cyc, FalkorWr_1XYZ_1cyc]>],
                                      (instrs MOVaddr, MOVaddrBA, MOVaddrCP, MOVaddrEXT, MOVaddrJT, MOVaddrTLS)>;
def : InstRW<[WriteSequence<[FalkorWr_1LD_3cyc, FalkorWr_1XYZ_1cyc]>],
                                      (instrs LOADgot)>;

// Other Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1LD_0cyc],     (instrs CLREX, DMB, DSB)>;
def : InstRW<[FalkorWr_1none_0cyc],   (instrs BRK, DCPS1, DCPS2, DCPS3, HINT, HLT, HVC, ISB, SMC, SVC)>;
def : InstRW<[FalkorWr_1ST_0cyc],     (instrs SYSxt, SYSLxt)>;
def : InstRW<[FalkorWr_1Z_0cyc],      (instrs MSRpstateImm1, MSRpstateImm4)>;

def : InstRW<[FalkorWr_1LD_3cyc, FalkorReadIncLd],
                                      (instregex "^(LDAR(B|H|W|X)|LDAXR(B|H|W|X)|LDXR(B|H|W|X))$")>;
def : InstRW<[FalkorWr_1LD_3cyc, FalkorWr_none_3cyc, FalkorReadIncLd],
                                      (instregex "^(LDAXP(W|X)|LDXP(W|X))$")>;
def : InstRW<[FalkorWr_1LD_3cyc],     (instrs MRS, MOVbaseTLS)>;

def : InstRW<[FalkorWr_1LD_1Z_3cyc],  (instrs DRPS)>;

def : InstRW<[FalkorWr_1SD_1ST_0cyc], (instrs MSR)>;
def : InstRW<[FalkorWr_1SD_1ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                      (instrs STNPWi, STNPXi)>;
def : InstRW<[FalkorWr_2LD_1Z_3cyc],  (instrs ERET)>;

def : InstRW<[FalkorWr_1ST_1SD_1LD_3cyc], (instregex "^LDC.*$")>;
def : InstRW<[FalkorWr_1ST_1SD_1LD_0cyc, ReadDefault, FalkorReadIncSt],
                                      (instregex "^STLR(B|H|W|X)$")>;
def : InstRW<[FalkorWr_1ST_1SD_1LD_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                      (instregex "^STXP(W|X)$")>;
def : InstRW<[FalkorWr_1ST_1SD_1LD_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                      (instregex "^STXR(B|H|W|X)$")>;

def : InstRW<[FalkorWr_2LD_1ST_1SD_3cyc, ReadDefault, ReadDefault, ReadDefault, FalkorReadIncSt],
                                      (instregex "^STLXP(W|X)$")>;
def : InstRW<[FalkorWr_2LD_1ST_1SD_3cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                      (instregex "^STLXR(B|H|W|X)$")>;

// Store Instructions
// -----------------------------------------------------------------------------
def : InstRW<[FalkorWr_1SD_1ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                          (instregex "^STP(W|X)i$")>;
def : InstRW<[FalkorWr_StInc_none_2cyc, FalkorWr_1SD_1ST_0cyc, ReadDefault, ReadDefault, FalkorReadIncSt],
                                          (instregex "^STP(W|X)(post|pre)$")>;
def : InstRW<[FalkorWr_1SD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                          (instregex "^STR(BB|HH|W|X)ui$")>;
def : InstRW<[FalkorWr_StInc_none_2cyc, FalkorWr_1SD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                          (instregex "^STR(BB|HH|W|X)(post|pre)$")>;
def : InstRW<[FalkorWr_STRro, ReadDefault, FalkorReadIncSt],
                                          (instregex "^STR(BB|HH|W|X)ro(W|X)$")>;
def : InstRW<[FalkorWr_1SD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                          (instregex "^STTR(B|H|W|X)i$")>;
def : InstRW<[FalkorWr_1SD_1ST_0cyc, ReadDefault, FalkorReadIncSt],
                                          (instregex "^STUR(BB|HH|W|X)i$")>;

