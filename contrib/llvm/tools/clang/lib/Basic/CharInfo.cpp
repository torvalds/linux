//===--- CharInfo.cpp - Static Data for Classifying ASCII Characters ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/CharInfo.h"

using namespace clang::charinfo;

// Statically initialize CharInfo table based on ASCII character set
// Reference: FreeBSD 7.2 /usr/share/misc/ascii
const uint16_t clang::charinfo::InfoTable[256] = {
  // 0 NUL         1 SOH         2 STX         3 ETX
  // 4 EOT         5 ENQ         6 ACK         7 BEL
  0           , 0           , 0           , 0           ,
  0           , 0           , 0           , 0           ,
  // 8 BS          9 HT         10 NL         11 VT
  //12 NP         13 CR         14 SO         15 SI
  0           , CHAR_HORZ_WS, CHAR_VERT_WS, CHAR_HORZ_WS,
  CHAR_HORZ_WS, CHAR_VERT_WS, 0           , 0           ,
  //16 DLE        17 DC1        18 DC2        19 DC3
  //20 DC4        21 NAK        22 SYN        23 ETB
  0           , 0           , 0           , 0           ,
  0           , 0           , 0           , 0           ,
  //24 CAN        25 EM         26 SUB        27 ESC
  //28 FS         29 GS         30 RS         31 US
  0           , 0           , 0           , 0           ,
  0           , 0           , 0           , 0           ,
  //32 SP         33  !         34  "         35  #
  //36  $         37  %         38  &         39  '
  CHAR_SPACE  , CHAR_RAWDEL , CHAR_RAWDEL , CHAR_RAWDEL ,
  CHAR_PUNCT  , CHAR_RAWDEL , CHAR_RAWDEL , CHAR_RAWDEL ,
  //40  (         41  )         42  *         43  +
  //44  ,         45  -         46  .         47  /
  CHAR_PUNCT  , CHAR_PUNCT  , CHAR_RAWDEL , CHAR_RAWDEL ,
  CHAR_RAWDEL , CHAR_RAWDEL , CHAR_PERIOD , CHAR_RAWDEL ,
  //48  0         49  1         50  2         51  3
  //52  4         53  5         54  6         55  7
  CHAR_DIGIT  , CHAR_DIGIT  , CHAR_DIGIT  , CHAR_DIGIT  ,
  CHAR_DIGIT  , CHAR_DIGIT  , CHAR_DIGIT  , CHAR_DIGIT  ,
  //56  8         57  9         58  :         59  ;
  //60  <         61  =         62  >         63  ?
  CHAR_DIGIT  , CHAR_DIGIT  , CHAR_RAWDEL , CHAR_RAWDEL ,
  CHAR_RAWDEL , CHAR_RAWDEL , CHAR_RAWDEL , CHAR_RAWDEL ,
  //64  @         65  A         66  B         67  C
  //68  D         69  E         70  F         71  G
  CHAR_PUNCT  , CHAR_XUPPER , CHAR_XUPPER , CHAR_XUPPER ,
  CHAR_XUPPER , CHAR_XUPPER , CHAR_XUPPER , CHAR_UPPER  ,
  //72  H         73  I         74  J         75  K
  //76  L         77  M         78  N         79  O
  CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  ,
  CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  ,
  //80  P         81  Q         82  R         83  S
  //84  T         85  U         86  V         87  W
  CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  ,
  CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  ,
  //88  X         89  Y         90  Z         91  [
  //92  \         93  ]         94  ^         95  _
  CHAR_UPPER  , CHAR_UPPER  , CHAR_UPPER  , CHAR_RAWDEL ,
  CHAR_PUNCT  , CHAR_RAWDEL , CHAR_RAWDEL , CHAR_UNDER  ,
  //96  `         97  a         98  b         99  c
  //100  d       101  e        102  f        103  g
  CHAR_PUNCT  , CHAR_XLOWER , CHAR_XLOWER , CHAR_XLOWER ,
  CHAR_XLOWER , CHAR_XLOWER , CHAR_XLOWER , CHAR_LOWER  ,
  //104  h       105  i        106  j        107  k
  //108  l       109  m        110  n        111  o
  CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  ,
  CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  ,
  //112  p       113  q        114  r        115  s
  //116  t       117  u        118  v        119  w
  CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  ,
  CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  ,
  //120  x       121  y        122  z        123  {
  //124  |       125  }        126  ~        127 DEL
  CHAR_LOWER  , CHAR_LOWER  , CHAR_LOWER  , CHAR_RAWDEL ,
  CHAR_RAWDEL , CHAR_RAWDEL , CHAR_RAWDEL , 0
};
