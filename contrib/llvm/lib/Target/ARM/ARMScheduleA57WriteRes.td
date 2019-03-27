//=- ARMScheduleA57WriteRes.td - ARM Cortex-A57 Write Res ---*- tablegen -*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Contains all of the Cortex-A57 specific SchedWriteRes types. The approach
// below is to define a generic SchedWriteRes for every combination of
// latency and microOps. The naming conventions is to use a prefix, one field
// for latency, and one or more microOp count/type designators.
//   Prefix: A57Write
//   Latency: #cyc
//   MicroOp Count/Types: #(B|I|M|L|S|X|W|V)
//
// e.g. A57Write_6cyc_1I_6S_4V means the total latency is 6 and there are
//      11 micro-ops to be issued as follows: one to I pipe, six to S pipes and
//      four to V pipes.
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Define Generic 1 micro-op types

def A57Write_5cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 5;  }
def A57Write_5cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 5;  }
def A57Write_5cyc_1W  : SchedWriteRes<[A57UnitW]> { let Latency = 5;  }
def A57Write_10cyc_1V : SchedWriteRes<[A57UnitV]> { let Latency = 10; }
def A57Write_17cyc_1W : SchedWriteRes<[A57UnitW]> { let Latency = 17;
                                                    let ResourceCycles = [17]; }
def A57Write_18cyc_1X : SchedWriteRes<[A57UnitX]> { let Latency = 18;
                                                    let ResourceCycles = [18]; }
def A57Write_19cyc_1M : SchedWriteRes<[A57UnitM]> { let Latency = 19;
                                                    let ResourceCycles = [19]; }
def A57Write_20cyc_1M : SchedWriteRes<[A57UnitM]> { let Latency = 20;
                                                    let ResourceCycles = [20]; }
def A57Write_1cyc_1B  : SchedWriteRes<[A57UnitB]> { let Latency = 1;  }
def A57Write_1cyc_1I  : SchedWriteRes<[A57UnitI]> { let Latency = 1;  }
def A57Write_2cyc_1I  : SchedWriteRes<[A57UnitI]> { let Latency = 2;  }
def A57Write_3cyc_1I  : SchedWriteRes<[A57UnitI]> { let Latency = 3;  }
def A57Write_1cyc_1S  : SchedWriteRes<[A57UnitS]> { let Latency = 1;  }
def A57Write_2cyc_1S  : SchedWriteRes<[A57UnitS]> { let Latency = 2;  }
def A57Write_3cyc_1S  : SchedWriteRes<[A57UnitS]> { let Latency = 3;  }
def A57Write_2cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 2;  }
def A57Write_32cyc_1W : SchedWriteRes<[A57UnitW]> { let Latency = 32;
                                                    let ResourceCycles = [32]; }
def A57Write_32cyc_1X : SchedWriteRes<[A57UnitX]> { let Latency = 32;
                                                    let ResourceCycles = [32]; }
def A57Write_35cyc_1M : SchedWriteRes<[A57UnitM]> { let Latency = 35;
                                                    let ResourceCycles = [35]; }
def A57Write_3cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 3;  }
def A57Write_3cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 3;  }
def A57Write_3cyc_1W  : SchedWriteRes<[A57UnitW]> { let Latency = 3;  }
def A57Write_3cyc_1X  : SchedWriteRes<[A57UnitX]> { let Latency = 3;  }

// A57Write_3cyc_1L - A57Write_20cyc_1L
foreach Lat = 3-20 in {
  def A57Write_#Lat#cyc_1L : SchedWriteRes<[A57UnitL]> {
    let Latency = Lat;
  }
}

// A57Write_4cyc_1S - A57Write_16cyc_1S
foreach Lat = 4-16 in {
  def A57Write_#Lat#cyc_1S : SchedWriteRes<[A57UnitS]> {
    let Latency = Lat;
  }
}

def A57Write_4cyc_1M  : SchedWriteRes<[A57UnitL]> { let Latency = 4;  }
def A57Write_4cyc_1X  : SchedWriteRes<[A57UnitX]> { let Latency = 4;  }
def A57Write_4cyc_1W  : SchedWriteRes<[A57UnitW]> { let Latency = 4;  }
def A57Write_5cyc_1X  : SchedWriteRes<[A57UnitX]> { let Latency = 5;  }
def A57Write_6cyc_1X  : SchedWriteRes<[A57UnitX]> { let Latency = 6;  }
def A57Write_6cyc_1W  : SchedWriteRes<[A57UnitW]> { let Latency = 6;  }
def A57Write_8cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 8;  }
def A57Write_9cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 9;  }
def A57Write_6cyc_1M  : SchedWriteRes<[A57UnitM]> { let Latency = 6;  }
def A57Write_6cyc_1V  : SchedWriteRes<[A57UnitV]> { let Latency = 6;  }


//===----------------------------------------------------------------------===//
// Define Generic 2 micro-op types

def A57Write_64cyc_2X    : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 64;
  let NumMicroOps = 2;
  let ResourceCycles = [32, 32];
}
def A57Write_6cyc_1I_1L  : SchedWriteRes<[A57UnitI,
                                          A57UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_6cyc_1V_1X  : SchedWriteRes<[A57UnitV,
                                          A57UnitX]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_7cyc_1V_1X  : SchedWriteRes<[A57UnitV,
                                          A57UnitX]> {
  let Latency     = 7;
  let NumMicroOps = 2;
}
def A57Write_8cyc_1L_1V  : SchedWriteRes<[A57UnitL,
                                          A57UnitV]> {
  let Latency     = 8;
  let NumMicroOps = 2;
}
def A57Write_9cyc_1L_1V  : SchedWriteRes<[A57UnitL,
                                          A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 2;
}
def A57Write_9cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 9;
  let NumMicroOps = 2;
}
def A57Write_8cyc_2X     : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 8;
  let NumMicroOps = 2;
}
def A57Write_6cyc_2L     : SchedWriteRes<[A57UnitL, A57UnitL]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_6cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_6cyc_2W     : SchedWriteRes<[A57UnitW, A57UnitW]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_5cyc_1I_1L  : SchedWriteRes<[A57UnitI,
                                          A57UnitL]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_5cyc_1I_1M  : SchedWriteRes<[A57UnitI,
                                          A57UnitM]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_5cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_5cyc_2X     : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 5;
  let NumMicroOps = 2;
}
def A57Write_10cyc_1L_1V : SchedWriteRes<[A57UnitL,
                                          A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 2;
}
def A57Write_10cyc_2V    : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 2;
}
def A57Write_1cyc_1B_1I  : SchedWriteRes<[A57UnitB,
                                          A57UnitI]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}
def A57Write_1cyc_1I_1S  : SchedWriteRes<[A57UnitI,
                                          A57UnitS]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}
def A57Write_1cyc_1S_1I  : SchedWriteRes<[A57UnitS,
                                          A57UnitI]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}
def A57Write_2cyc_1S_1I  : SchedWriteRes<[A57UnitS,
                                          A57UnitI]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_3cyc_1S_1I  : SchedWriteRes<[A57UnitS,
                                          A57UnitI]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_1cyc_1S_1M  : SchedWriteRes<[A57UnitS,
                                          A57UnitM]> {
  let Latency     = 1;
  let NumMicroOps = 2;
}
def A57Write_2cyc_1B_1I  : SchedWriteRes<[A57UnitB,
                                          A57UnitI]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_3cyc_1B_1I  : SchedWriteRes<[A57UnitB,
                                          A57UnitI]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_6cyc_1B_1L  : SchedWriteRes<[A57UnitB,
                                          A57UnitI]> {
  let Latency     = 6;
  let NumMicroOps = 2;
}
def A57Write_2cyc_1I_1M  : SchedWriteRes<[A57UnitI,
                                          A57UnitM]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_2cyc_2S     : SchedWriteRes<[A57UnitS, A57UnitS]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_2cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 2;
  let NumMicroOps = 2;
}
def A57Write_36cyc_2X    : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 36;
  let NumMicroOps = 2;
  let ResourceCycles = [18, 18];
}
def A57Write_3cyc_1I_1M  : SchedWriteRes<[A57UnitI,
                                          A57UnitM]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_4cyc_1I_1M  : SchedWriteRes<[A57UnitI,
                                          A57UnitM]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}

// A57Write_3cyc_1L_1I - A57Write_20cyc_1L_1I
foreach Lat = 3-20 in {
  def A57Write_#Lat#cyc_1L_1I : SchedWriteRes<[A57UnitL, A57UnitI]> {
    let Latency = Lat; let NumMicroOps = 2;
  }
}

def A57Write_3cyc_1I_1S  : SchedWriteRes<[A57UnitI,
                                          A57UnitS]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_3cyc_1S_1V  : SchedWriteRes<[A57UnitS,
                                          A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}
def A57Write_4cyc_1S_1V  : SchedWriteRes<[A57UnitS,
                                          A57UnitV]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}
def A57Write_3cyc_2V     : SchedWriteRes<[A57UnitV, A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 2;
}

// A57Write_4cyc_1S_1I - A57Write_16cyc_1S_1I
foreach Lat = 4-16 in {
  def A57Write_#Lat#cyc_1S_1I : SchedWriteRes<[A57UnitS, A57UnitI]> {
    let Latency = Lat; let NumMicroOps = 2;
  }
}

def A57Write_4cyc_2X     : SchedWriteRes<[A57UnitX, A57UnitX]> {
  let Latency     = 4;
  let NumMicroOps = 2;
}


//===----------------------------------------------------------------------===//
// Define Generic 3 micro-op types

def A57Write_10cyc_3V       : SchedWriteRes<[A57UnitV, A57UnitV, A57UnitV]> {
  let Latency     = 10;
  let NumMicroOps = 3;
}
def A57Write_2cyc_1I_2S     : SchedWriteRes<[A57UnitI,
                                             A57UnitS, A57UnitS]> {
  let Latency     = 2;
  let NumMicroOps = 3;
}
def A57Write_3cyc_1I_1S_1V  : SchedWriteRes<[A57UnitI,
                                             A57UnitS,
                                             A57UnitV]> {
  let Latency     = 3;
  let NumMicroOps = 3;
}
def A57Write_3cyc_1S_1V_1I  : SchedWriteRes<[A57UnitS,
                                             A57UnitV,
                                             A57UnitI]> {
  let Latency     = 3;
  let NumMicroOps = 3;
}
def A57Write_4cyc_1S_1V_1I  : SchedWriteRes<[A57UnitS,
                                             A57UnitV,
                                             A57UnitI]> {
  let Latency     = 4;
  let NumMicroOps = 3;
}
def A57Write_4cyc_1I_1L_1M  : SchedWriteRes<[A57UnitI, A57UnitL, A57UnitM]> {
  let Latency     = 4;
  let NumMicroOps = 3;
}
def A57Write_8cyc_1L_1V_1I  : SchedWriteRes<[A57UnitL,
                                             A57UnitV,
                                             A57UnitI]> {
  let Latency     = 8;
  let NumMicroOps = 3;
}
def A57Write_9cyc_1L_1V_1I  : SchedWriteRes<[A57UnitL,
                                             A57UnitV,
                                             A57UnitI]> {
  let Latency     = 9;
  let NumMicroOps = 3;
}
