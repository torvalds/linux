/*
                            __  __            _
                         ___\ \/ /_ __   __ _| |_
                        / _ \\  /| '_ \ / _` | __|
                       |  __//  \| |_) | (_| | |_
                        \___/_/\_\ .__/ \__,_|\__|
                                 |_| XML parser

   Copyright (c) 1997-2000 Thai Open Source Software Center Ltd
   Copyright (c) 2000-2017 Expat development team
   Licensed under the MIT license:

   Permission is  hereby granted,  free of charge,  to any  person obtaining
   a  copy  of  this  software   and  associated  documentation  files  (the
   "Software"),  to  deal in  the  Software  without restriction,  including
   without  limitation the  rights  to use,  copy,  modify, merge,  publish,
   distribute, sublicense, and/or sell copies of the Software, and to permit
   persons  to whom  the Software  is  furnished to  do so,  subject to  the
   following conditions:

   The above copyright  notice and this permission notice  shall be included
   in all copies or substantial portions of the Software.

   THE  SOFTWARE  IS  PROVIDED  "AS  IS",  WITHOUT  WARRANTY  OF  ANY  KIND,
   EXPRESS  OR IMPLIED,  INCLUDING  BUT  NOT LIMITED  TO  THE WARRANTIES  OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
   NO EVENT SHALL THE AUTHORS OR  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
   DAMAGES OR  OTHER LIABILITY, WHETHER  IN AN  ACTION OF CONTRACT,  TORT OR
   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
   USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

/* 0x00 */ BT_NONXML, BT_NONXML, BT_NONXML, BT_NONXML,
/* 0x04 */ BT_NONXML, BT_NONXML, BT_NONXML, BT_NONXML,
/* 0x08 */ BT_NONXML, BT_S, BT_LF, BT_NONXML,
/* 0x0C */ BT_NONXML, BT_CR, BT_NONXML, BT_NONXML,
/* 0x10 */ BT_NONXML, BT_NONXML, BT_NONXML, BT_NONXML,
/* 0x14 */ BT_NONXML, BT_NONXML, BT_NONXML, BT_NONXML,
/* 0x18 */ BT_NONXML, BT_NONXML, BT_NONXML, BT_NONXML,
/* 0x1C */ BT_NONXML, BT_NONXML, BT_NONXML, BT_NONXML,
/* 0x20 */ BT_S, BT_EXCL, BT_QUOT, BT_NUM,
/* 0x24 */ BT_OTHER, BT_PERCNT, BT_AMP, BT_APOS,
/* 0x28 */ BT_LPAR, BT_RPAR, BT_AST, BT_PLUS,
/* 0x2C */ BT_COMMA, BT_MINUS, BT_NAME, BT_SOL,
/* 0x30 */ BT_DIGIT, BT_DIGIT, BT_DIGIT, BT_DIGIT,
/* 0x34 */ BT_DIGIT, BT_DIGIT, BT_DIGIT, BT_DIGIT,
/* 0x38 */ BT_DIGIT, BT_DIGIT, BT_COLON, BT_SEMI,
/* 0x3C */ BT_LT, BT_EQUALS, BT_GT, BT_QUEST,
/* 0x40 */ BT_OTHER, BT_HEX, BT_HEX, BT_HEX,
/* 0x44 */ BT_HEX, BT_HEX, BT_HEX, BT_NMSTRT,
/* 0x48 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x4C */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x50 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x54 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x58 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_LSQB,
/* 0x5C */ BT_OTHER, BT_RSQB, BT_OTHER, BT_NMSTRT,
/* 0x60 */ BT_OTHER, BT_HEX, BT_HEX, BT_HEX,
/* 0x64 */ BT_HEX, BT_HEX, BT_HEX, BT_NMSTRT,
/* 0x68 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x6C */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x70 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x74 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_NMSTRT,
/* 0x78 */ BT_NMSTRT, BT_NMSTRT, BT_NMSTRT, BT_OTHER,
/* 0x7C */ BT_VERBAR, BT_OTHER, BT_OTHER, BT_OTHER,
