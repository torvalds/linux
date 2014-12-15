#ifndef OV5640_FIRMWARE_H
#define OV5640_FIRMWARE_H
#define CMD_MAIN	0x3022
#define CMD_ACK		0x3023
#define CMD_PARA0	0x3024
#define CMD_PARA1	0x3025
#define CMD_PARA2	0x3026
#define CMD_PARA3	0x3027
#define CMD_PARA4	0x3028
#define FW_STATUS	0x3029
struct aml_camera_i2c_fig_s HM5065_script_step[] = {

{0xffff,0x01},  //	; MCU bypass;
{0x9000,0x03},  //	; Enable Ram and enable Write;
{0xA000,0x90},  //	;    MOV      DPTR,#fpInputRange(0x0C56)
{0xA001,0x0C},  //	; 
{0xA002,0x56},  //	; 
{0xA003,0xE0},  //	;    MOVX     A,@DPTR
{0xA004,0xFE},  //	;    MOV      R6,A
{0xA005,0xA3},  //	;    INC      DPTR
{0xA006,0xE0},  //	;    MOVX     A,@DPTR
{0xA007,0xFF},  //	;    MOV      R7,A
{0xA008,0x12},  //	;    LCALL    FPAlu_FloatToInt16(C:4285)
{0xA009,0x42},  //	; 
{0xA00A,0x85},  //	; 
{0xA00B,0x90},  //	;    MOV      DPTR,#0x01B7  (0x0B4D)
{0xA00C,0x01},  //	; 
{0xA00D,0xB7},  //	; 
{0xA00E,0xEE},  //	;    MOV      A,R6
{0xA00F,0xF0},  //	;    MOVX     @DPTR,A
{0xA010,0xFC},  //	;    MOV      R4,A
{0xA011,0xA3},  //	;    INC      DPTR
{0xA012,0xEF},  //	;    MOV      A,R7
{0xA013,0xF0},  //	;    MOVX     @DPTR,A
{0xA014,0xFD},  //	;    MOV      R5,A
{0xA015,0x90},  //	;    MOV      DPTR,#0x0605
{0xA016,0x06},  //	; 
{0xA017,0x05},  //	; 
{0xA018,0xE0},  //	;    MOVX     A,@DPTR
{0xA019,0x75},  //	;    MOV      B(0xF0),#0x02
{0xA01A,0xF0},  //	; 
{0xA01B,0x02},  //	; 
{0xA01C,0xA4},  //	;    MUL      AB
{0xA01D,0x2D},  //	;    ADD      A,R5
{0xA01E,0xFF},  //	;    MOV      R7,A
{0xA01F,0xE5},  //	;    MOV      A,B(0xF0)
{0xA020,0xF0},  //	; 
{0xA021,0x3C},  //	;    ADDC     A,R4
{0xA022,0xFE},  //	;    MOV      R6,A
{0xA023,0xAB},  //	;    MOV      R3 07
{0xA024,0x07},  //	; 
{0xA025,0xFA},  //	;    MOV      R2,A
{0xA026,0x33},  //	;    RLC      A
{0xA027,0x95},  //	;    SUBB     A,ACC(0xE0)
{0xA028,0xE0},  //	; 
{0xA029,0xF9},  //	;    MOV      R1,A
{0xA02A,0xF8},  //	;    MOV      R0,A
{0xA02B,0x90},  //	;    MOV      DPTR,#0x0B4B
{0xA02C,0x0B},  //	; 
{0xA02D,0x4B},  //	; 
{0xA02E,0xE0},  //	;    MOVX     A,@DPTR
{0xA02F,0xFE},  //	;    MOV      R6,A
{0xA030,0xA3},  //	;    INC      DPTR
{0xA031,0xE0},  //	;    MOVX     A,@DPTR
{0xA032,0xFF},  //	;    MOV      R7,A
{0xA033,0xEE},  //	;    MOV      A,R6
{0xA034,0x33},  //	;    RLC      A
{0xA035,0x95},  //	;    SUBB     A,ACC(0xE0)
{0xA036,0xE0},  //	; 
{0xA037,0xFD},  //	;    MOV      R5,A
{0xA038,0xFC},  //	;    MOV      R4,A
{0xA039,0x12},  //	;    LCALL    C?LMUL(C:0C7B)
{0xA03A,0x0C},  //	; 
{0xA03B,0x7B},  //	; 
{0xA03C,0x90},  //	;    MOV      DPTR,#0x01B9(0x0B4F)
{0xA03D,0x01},  //	; 
{0xA03E,0xB9},  //	; 
{0xA03F,0x12},  //	;    LCALL    C?LSTXDATA(C:0E05)
{0xA040,0x0E},  //	; 
{0xA041,0x05},  //	; 
{0xA042,0x90},  //	;    MOV      DPTR,#0x01B9(0x0B4F)
{0xA043,0x01},  //	; 
{0xA044,0xB9},  //	; 
{0xA045,0xE0},  //	;    MOVX     A,@DPTR
{0xA046,0xFC},  //	;    MOV      R4,A
{0xA047,0xA3},  //	;    INC      DPTR
{0xA048,0xE0},  //	;    MOVX     A,@DPTR
{0xA049,0xFD},  //	;    MOV      R5,A
{0xA04A,0xA3},  //	;    INC      DPTR
{0xA04B,0xE0},  //	;    MOVX     A,@DPTR
{0xA04C,0xFE},  //	;    MOV      R6,A
{0xA04D,0xA3},  //	;    INC      DPTR
{0xA04E,0xE0},  //	;    MOVX     A,@DPTR
{0xA04F,0xFF},  //	;    MOV      R7,A
{0xA050,0x78},  //	;    MOV      R0,#g_fTimer0TimeOut(0x08)
{0xA051,0x08},  //	; 
{0xA052,0x12},  //	;    LCALL    C?ULSHR(C:0DBF)
{0xA053,0x0D},  //	; 
{0xA054,0xBF},  //	; 
{0xA055,0xA8},  //	;    MOV      R0,uwDelay1000(0x04)
{0xA056,0x04},  //	; 
{0xA057,0xA9},  //	;    MOV      R1 05
{0xA058,0x05},  //	; 
{0xA059,0xAA},  //	;    MOV      R2,uwDelay100(0x06)
{0xA05A,0x06},  //	; 
{0xA05B,0xAB},  //	;    MOV      R3 07
{0xA05C,0x07},  //	; 
{0xA05D,0x90},  //	;    MOV      DPTR,#0x0B49
{0xA05E,0x0B},  //	; 
{0xA05F,0x49},  //	; 
{0xA060,0xE0},  //	;    MOVX     A,@DPTR
{0xA061,0xFE},  //	;    MOV      R6,A
{0xA062,0xA3},  //	;    INC      DPTR
{0xA063,0xE0},  //	;    MOVX     A,@DPTR
{0xA064,0xFF},  //	;    MOV      R7,A
{0xA065,0xEE},  //	;    MOV      A,R6
{0xA066,0x33},  //	;    RLC      A
{0xA067,0x95},  //	;    SUBB     A,ACC(0xE0)
{0xA068,0xE0},  //	; 
{0xA069,0xFD},  //	;    MOV      R5,A
{0xA06A,0xFC},  //	;    MOV      R4,A
{0xA06B,0xC3},  //	;    CLR      C
{0xA06C,0xEF},  //	;    MOV      A,R7
{0xA06D,0x9B},  //	;    SUBB     A,R3
{0xA06E,0xFF},  //	;    MOV      R7,A
{0xA06F,0xEE},  //	;    MOV      A,R6
{0xA070,0x9A},  //	;    SUBB     A,R2
{0xA071,0xFE},  //	;    MOV      R6,A
{0xA072,0xED},  //	;    MOV      A,R5
{0xA073,0x99},  //	;    SUBB     A,R1
{0xA074,0xFD},  //	;    MOV      R5,A
{0xA075,0xEC},  //	;    MOV      A,R4
{0xA076,0x98},  //	;    SUBB     A,R0
{0xA077,0xFC},  //	;    MOV      R4,A
{0xA078,0x78},  //	;    MOV      R0,#0x01
{0xA079,0x01},  //	; 
{0xA07A,0x12},  //	;    LCALL    C?ULSHR(C:0DBF)
{0xA07B,0x0D},  //	; 
{0xA07C,0xBF},  //	; 
{0xA07D,0x90},  //	;    MOV      DPTR,#m_pxwOffsetVector(0x0C4A)
{0xA07E,0x0C},  //	; 
{0xA07F,0x4A},  //	; 
{0xA080,0xE0},  //	;    MOVX     A,@DPTR
{0xA081,0xFC},  //	;    MOV      R4,A
{0xA082,0xA3},  //	;    INC      DPTR
{0xA083,0xE0},  //	;    MOVX     A,@DPTR
{0xA084,0xF5},  //	;    MOV      DPL(0x82),A
{0xA085,0x82},  //	; 
{0xA086,0x8C},  //	;    MOV      DPH(0x83),R4
{0xA087,0x83},  //	; 
{0xA088,0xC0},  //	;    PUSH     DPH(0x83)
{0xA089,0x83},  //	; 
{0xA08A,0xC0},  //	;    PUSH     DPL(0x82)
{0xA08B,0x82},  //	; 
{0xA08C,0x90},  //	;    MOV      DPTR,#0x0B48
{0xA08D,0x0B},  //	; 
{0xA08E,0x48},  //	; 
{0xA08F,0xE0},  //	;    MOVX     A,@DPTR
{0xA090,0xD0},  //	;    POP      DPL(0x82)
{0xA091,0x82},  //	; 
{0xA092,0xD0},  //	;    POP      DPH(0x83)
{0xA093,0x83},  //	; 
{0xA094,0x75},  //	;    MOV      B(0xF0),#0x02
{0xA095,0xF0},  //	; 
{0xA096,0x02},  //	; 
{0xA097,0x12},  //	;    LCALL    C?OFFXADD(C:0E45)
{0xA098,0x0E},  //	; 
{0xA099,0x45},  //	; 
{0xA09A,0xEE},  //	;    MOV      A,R6
{0xA09B,0xF0},  //	;    MOVX     @DPTR,A
{0xA09C,0xA3},  //	;    INC      DPTR
{0xA09D,0xEF},  //	;    MOV      A,R7
{0xA09E,0xF0},  //	;    MOVX     @DPTR,A
{0xA09F,0x02},  //	;    LJMP     C:BAD8
{0xA0A0,0xBA},  //	; 
{0xA0A1,0xD8},  //	; 
{0xA0A2,0x90},  //    ;         
{0xA0A3,0x30},  //    ;        MOV      DPTR,#0x0036
{0xA0A4,0x18},  //    ;        
{0xA0A5,0xe4},  //    ;        
{0xA0A6,0xf0},  //    ;        MOV      A,#0x00
{0xA0A7,0x74},  //    ;        
{0xA0A8,0x3f},  //    ;        MOVX     @DPTR,A
{0xA0A9,0xf0},  //    ;        INC      DPTR
{0xA0AA,0x22},  //    ;        INC      DPTR
{0xA0BF,0x90},  //	;    MOV      DPTR,#0x005E
{0xA0C0,0x00},  //	; 
{0xA0C1,0x5E},  //	; 
{0xA0C2,0xE0},  //	;    MOVX     A,@DPTR
{0xA0C3,0xFF},  //	;    MOV      R7,A
{0xA0C4,0x70},  //	;    JNZ      B00:A9AF
{0xA0C5,0x20},  //	; 
{0xA0C6,0x90},  //	;    MOV      DPTR,#Av2x2_H_Size(0x4704)
{0xA0C7,0x47},  //	; 
{0xA0C8,0x04},  //	; 
{0xA0C9,0x74},  //	;    MOV      A,#bInt_Event_Status(0x0A)
{0xA0CA,0x0A},  //	; 
{0xA0CB,0xF0},  //	;    MOVX     @DPTR,A
{0xA0CC,0xA3},  //	;    INC      DPTR
{0xA0CD,0x74},  //	;    MOV      A,#0x30
{0xA0CE,0x30},  //	; 
{0xA0CF,0xF0},  //	;    MOVX     @DPTR,A
{0xA0D0,0x90},  //	;    MOV      DPTR,#Av2x2_V_Size(0x470C)
{0xA0D1,0x47},  //	; 
{0xA0D2,0x0C},  //	; 
{0xA0D3,0x74},  //	;    MOV      A,#0x07
{0xA0D4,0x07},  //	; 
{0xA0D5,0xF0},  //	;    MOVX     @DPTR,A
{0xA0D6,0xA3},  //	;    INC      DPTR
{0xA0D7,0x74},  //	;    MOV      A,#IE(0xA8)
{0xA0D8,0xA8},  //	; 
{0xA0D9,0xF0},  //	;    MOVX     @DPTR,A
{0xA0DA,0x90},  //	;    MOV      DPTR,#Av2x2_Xscale(0x47A4)
{0xA0DB,0x47},  //	; 
{0xA0DC,0xA4},  //	; 
{0xA0DD,0x74},  //	;    MOV      A,#0x01
{0xA0DE,0x01},  //	; 
{0xA0DF,0xF0},  //	;    MOVX     @DPTR,A
{0xA0E0,0x90},  //	;    MOV      DPTR,#Av2x2_Yscale(0x47A8)
{0xA0E1,0x47},  //	; 
{0xA0E2,0xA8},  //	; 
{0xA0E3,0xF0},  //	;    MOVX     @DPTR,A
{0xA0E4,0x80},  //	;    SJMP     B00:A9FF
{0xA0E5,0x50},  //	; 
{0xA0E6,0xEF},  //	;    MOV      A,R7
{0xA0E7,0x64},  //	;    XRL      A,#0x01
{0xA0E8,0x01},  //	; 
{0xA0E9,0x60},  //	;    JZ       B00:A9B8
{0xA0EA,0x04},  //	; 
{0xA0EB,0xEF},  //	;    MOV      A,R7
{0xA0EC,0xB4},  //	;    CJNE     A,#0x03,B00:A9D8
{0xA0ED,0x03},  //	; 
{0xA0EE,0x20},  //	; 
{0xA0EF,0x90},  //	;    MOV      DPTR,#Av2x2_H_Size(0x4704)
{0xA0F0,0x47},  //	; 
{0xA0F1,0x04},  //	; 
{0xA0F2,0x74},  //	;    MOV      A,#0x05
{0xA0F3,0x05},  //	; 
{0xA0F4,0xF0},  //	;    MOVX     @DPTR,A
{0xA0F5,0xA3},  //	;    INC      DPTR
{0xA0F6,0x74},  //	;    MOV      A,#0x18
{0xA0F7,0x18},  //	; 
{0xA0F8,0xF0},  //	;    MOVX     @DPTR,A
{0xA0F9,0x90},  //	;    MOV      DPTR,#Av2x2_V_Size(0x470C)
{0xA0FA,0x47},  //	; 
{0xA0FB,0x0C},  //	; 
{0xA0FC,0x74},  //	;    MOV      A,#0x03
{0xA0FD,0x03},  //	; 
{0xA0FE,0xF0},  //	;    MOVX     @DPTR,A
{0xA0FF,0xA3},  //	;    INC      DPTR
{0xA100,0x74},  //	;    MOV      A,#m_fDitherBitFormat(0xD4)
{0xA101,0xD4},  //	; 
{0xA102,0xF0},  //	;    MOVX     @DPTR,A
{0xA103,0x90},  //	;    MOV      DPTR,#Av2x2_Xscale(0x47A4)
{0xA104,0x47},  //	; 
{0xA105,0xA4},  //	; 
{0xA106,0x74},  //	;    MOV      A,#0x02
{0xA107,0x02},  //	; 
{0xA108,0xF0},  //	;    MOVX     @DPTR,A
{0xA109,0x90},  //	;    MOV      DPTR,#Av2x2_Yscale(0x47A8)
{0xA10A,0x47},  //	; 
{0xA10B,0xA8},  //	; 
{0xA10C,0xF0},  //	;    MOVX     @DPTR,A
{0xA10D,0x80},  //	;    SJMP     B00:A9FF
{0xA10E,0x27},  //	; 
{0xA10F,0xEF},  //	;    MOV      A,R7
{0xA110,0x64},  //	;    XRL      A,#0x02
{0xA111,0x02},  //	; 
{0xA112,0x60},  //	;    JZ       B00:A9E1
{0xA113,0x04},  //	; 
{0xA114,0xEF},  //	;    MOV      A,R7
{0xA115,0xB4},  //	;    CJNE     A,#uwDelay1000(0x04),B00:A9FF
{0xA116,0x04},  //	; 
{0xA117,0x1E},  //	; 
{0xA118,0x90},  //	;    MOV      DPTR,#Av2x2_H_Size(0x4704)
{0xA119,0x47},  //	; 
{0xA11A,0x04},  //	; 
{0xA11B,0x74},  //	;    MOV      A,#0x02
{0xA11C,0x02},  //	; 
{0xA11D,0xF0},  //	;    MOVX     @DPTR,A
{0xA11E,0xA3},  //	;    INC      DPTR
{0xA11F,0x74},  //	;    MOV      A,#TH0(0x8C)
{0xA120,0x8C},  //	; 
{0xA121,0xF0},  //	;    MOVX     @DPTR,A
{0xA122,0x90},  //	;    MOV      DPTR,#Av2x2_V_Size(0x470C)
{0xA123,0x47},  //	; 
{0xA124,0x0C},  //	; 
{0xA125,0x74},  //	;    MOV      A,#0x01
{0xA126,0x01},  //	; 
{0xA127,0xF0},  //	;    MOVX     @DPTR,A
{0xA128,0xA3},  //	;    INC      DPTR
{0xA129,0x74},  //	;    MOV      A,#0xEA
{0xA12A,0xEA},  //	; 
{0xA12B,0xF0},  //	;    MOVX     @DPTR,A
{0xA12C,0x90},  //	;    MOV      DPTR,#Av2x2_Xscale(0x47A4)
{0xA12D,0x47},  //	; 
{0xA12E,0xA4},  //	; 
{0xA12F,0x74},  //	;    MOV      A,#uwDelay1000(0x04)
{0xA130,0x04},  //	; 
{0xA131,0xF0},  //	;    MOVX     @DPTR,A
{0xA132,0x90},  //	;    MOV      DPTR,#Av2x2_Yscale(0x47A8)
{0xA133,0x47},  //	; 
{0xA134,0xA8},  //	; 
{0xA135,0xF0},  //	;    MOVX     @DPTR,A
{0xA136,0x22},  //	;    RTN
{0xA137,0x74},  //	;    MOV      A,#uwDelay1000(0x04)
{0xA138,0x04},  //	; 
{0xA139,0xF0},  //	;    MOVX     @DPTR,A
{0xA13A,0xA3},  //	;    INC      DPTR
{0xA13B,0x74},  //	;    MOV      A,#ZoomPanControl(0x20)
{0xA13C,0x20},  //	; 
{0xA13D,0xF0},  //	;    MOVX     @DPTR,A
{0xA13E,0xE4},  //	;    CLR      A
{0xA13F,0xF5},  //	;    MOV      0x22,A
{0xA140,0x22},  //	; 
{0xA141,0xE5},  //	;    MOV      A 22
{0xA142,0x22},  //	; 
{0xA143,0xC3},  //	;    CLR      C
{0xA144,0x94},  //	;    SUBB     A,#PipeSetupBank0(0x40)
{0xA145,0x40},  //	; 
{0xA146,0x40},  //	;    JC       B00:AB81
{0xA147,0x03},  //	; 
{0xA148,0x02},  //	;    LJMP     B00:AC33
{0xA149,0xF1},  //	; 
{0xA14A,0xFD},  //	; 
{0xA14B,0x90},  //	;    MOV      DPTR,#0x0ABA
{0xA14C,0x0A},  //	; 
{0xA14D,0xBA},  //	; 
{0xA14E,0xE0},  //	;    MOVX     A,@DPTR
{0xA14F,0xFE},  //	;    MOV      R6,A
{0xA150,0xA3},  //	;    INC      DPTR
{0xA151,0xE0},  //	;    MOVX     A,@DPTR
{0xA152,0xFF},  //	;    MOV      R7,A
{0xA153,0xF5},  //	;    MOV      DPL(0x82),A
{0xA154,0x82},  //	; 
{0xA155,0x8E},  //	;    MOV      DPH(0x83),R6
{0xA156,0x83},  //	; 
{0xA157,0xE0},  //	;    MOVX     A,@DPTR
{0xA158,0x54},  //	;    ANL      A,#0x70
{0xA159,0x70},  //	; 
{0xA15A,0xFD},  //	;    MOV      R5,A
{0xA15B,0xC4},  //	;    SWAP     A
{0xA15C,0x54},  //	;    ANL      A,#0x0F
{0xA15D,0x0F},  //	; 
{0xA15E,0xFD},  //	;    MOV      R5,A
{0xA15F,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA160,0x0A},  //	; 
{0xA161,0xBC},  //	; 
{0xA162,0xE0},  //	;    MOVX     A,@DPTR
{0xA163,0xFA},  //	;    MOV      R2,A
{0xA164,0xA3},  //	;    INC      DPTR
{0xA165,0xE0},  //	;    MOVX     A,@DPTR
{0xA166,0xF5},  //	;    MOV      DPL(0x82),A
{0xA167,0x82},  //	; 
{0xA168,0x8A},  //	;    MOV      DPH(0x83),R2
{0xA169,0x83},  //	; 
{0xA16A,0xED},  //	;    MOV      A,R5
{0xA16B,0xF0},  //	;    MOVX     @DPTR,A
{0xA16C,0x90},  //	;    MOV      DPTR,#0x0ABD
{0xA16D,0x0A},  //	; 
{0xA16E,0xBD},  //	; 
{0xA16F,0xE0},  //	;    MOVX     A,@DPTR
{0xA170,0x04},  //	;    INC      A
{0xA171,0xF0},  //	;    MOVX     @DPTR,A
{0xA172,0x70},  //	;    JNZ      B00:ABB0
{0xA173,0x06},  //	; 
{0xA174,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA175,0x0A},  //	; 
{0xA176,0xBC},  //	; 
{0xA177,0xE0},  //	;    MOVX     A,@DPTR
{0xA178,0x04},  //	;    INC      A
{0xA179,0xF0},  //	;    MOVX     @DPTR,A
{0xA17A,0x8F},  //	;    MOV      DPL(0x82),R7
{0xA17B,0x82},  //	; 
{0xA17C,0x8E},  //	;    MOV      DPH(0x83),R6
{0xA17D,0x83},  //	; 
{0xA17E,0xA3},  //	;    INC      DPTR
{0xA17F,0xE0},  //	;    MOVX     A,@DPTR
{0xA180,0xFF},  //	;    MOV      R7,A
{0xA181,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA182,0x0A},  //	; 
{0xA183,0xBC},  //	; 
{0xA184,0xE0},  //	;    MOVX     A,@DPTR
{0xA185,0xFC},  //	;    MOV      R4,A
{0xA186,0xA3},  //	;    INC      DPTR
{0xA187,0xE0},  //	;    MOVX     A,@DPTR
{0xA188,0xF5},  //	;    MOV      DPL(0x82),A
{0xA189,0x82},  //	; 
{0xA18A,0x8C},  //	;    MOV      DPH(0x83),R4
{0xA18B,0x83},  //	; 
{0xA18C,0xEF},  //	;    MOV      A,R7
{0xA18D,0xF0},  //	;    MOVX     @DPTR,A
{0xA18E,0x90},  //	;    MOV      DPTR,#0x0ABD
{0xA18F,0x0A},  //	; 
{0xA190,0xBD},  //	; 
{0xA191,0xE0},  //	;    MOVX     A,@DPTR
{0xA192,0x04},  //	;    INC      A
{0xA193,0xF0},  //	;    MOVX     @DPTR,A
{0xA194,0x70},  //	;    JNZ      B00:ABD2
{0xA195,0x06},  //	; 
{0xA196,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA197,0x0A},  //	; 
{0xA198,0xBC},  //	; 
{0xA199,0xE0},  //	;    MOVX     A,@DPTR
{0xA19A,0x04},  //	;    INC      A
{0xA19B,0xF0},  //	;    MOVX     @DPTR,A
{0xA19C,0x90},  //	;    MOV      DPTR,#0x0ABA
{0xA19D,0x0A},  //	; 
{0xA19E,0xBA},  //	; 
{0xA19F,0xE0},  //	;    MOVX     A,@DPTR
{0xA1A0,0xFE},  //	;    MOV      R6,A
{0xA1A1,0xA3},  //	;    INC      DPTR
{0xA1A2,0xE0},  //	;    MOVX     A,@DPTR
{0xA1A3,0xFF},  //	;    MOV      R7,A
{0xA1A4,0xF5},  //	;    MOV      DPL(0x82),A
{0xA1A5,0x82},  //	; 
{0xA1A6,0x8E},  //	;    MOV      DPH(0x83),R6
{0xA1A7,0x83},  //	; 
{0xA1A8,0xE0},  //	;    MOVX     A,@DPTR
{0xA1A9,0x54},  //	;    ANL      A,#0x07
{0xA1AA,0x07},  //	; 
{0xA1AB,0xFD},  //	;    MOV      R5,A
{0xA1AC,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA1AD,0x0A},  //	; 
{0xA1AE,0xBC},  //	; 
{0xA1AF,0xE0},  //	;    MOVX     A,@DPTR
{0xA1B0,0xFA},  //	;    MOV      R2,A
{0xA1B1,0xA3},  //	;    INC      DPTR
{0xA1B2,0xE0},  //	;    MOVX     A,@DPTR
{0xA1B3,0xF5},  //	;    MOV      DPL(0x82),A
{0xA1B4,0x82},  //	; 
{0xA1B5,0x8A},  //	;    MOV      DPH(0x83),R2
{0xA1B6,0x83},  //	; 
{0xA1B7,0xED},  //	;    MOV      A,R5
{0xA1B8,0xF0},  //	;    MOVX     @DPTR,A
{0xA1B9,0x90},  //	;    MOV      DPTR,#0x0ABD
{0xA1BA,0x0A},  //	; 
{0xA1BB,0xBD},  //	; 
{0xA1BC,0xE0},  //	;    MOVX     A,@DPTR
{0xA1BD,0x04},  //	;    INC      A
{0xA1BE,0xF0},  //	;    MOVX     @DPTR,A
{0xA1BF,0x70},  //	;    JNZ      B00:ABFD
{0xA1C0,0x06},  //	; 
{0xA1C1,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA1C2,0x0A},  //	; 
{0xA1C3,0xBC},  //	; 
{0xA1C4,0xE0},  //	;    MOVX     A,@DPTR
{0xA1C5,0x04},  //	;    INC      A
{0xA1C6,0xF0},  //	;    MOVX     @DPTR,A
{0xA1C7,0x8F},  //	;    MOV      DPL(0x82),R7
{0xA1C8,0x82},  //	; 
{0xA1C9,0x8E},  //	;    MOV      DPH(0x83),R6
{0xA1CA,0x83},  //	; 
{0xA1CB,0xA3},  //	;    INC      DPTR
{0xA1CC,0xA3},  //	;    INC      DPTR
{0xA1CD,0xE0},  //	;    MOVX     A,@DPTR
{0xA1CE,0xFF},  //	;    MOV      R7,A
{0xA1CF,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA1D0,0x0A},  //	; 
{0xA1D1,0xBC},  //	; 
{0xA1D2,0xE0},  //	;    MOVX     A,@DPTR
{0xA1D3,0xFC},  //	;    MOV      R4,A
{0xA1D4,0xA3},  //	;    INC      DPTR
{0xA1D5,0xE0},  //	;    MOVX     A,@DPTR
{0xA1D6,0xF5},  //	;    MOV      DPL(0x82),A
{0xA1D7,0x82},  //	; 
{0xA1D8,0x8C},  //	;    MOV      DPH(0x83),R4
{0xA1D9,0x83},  //	; 
{0xA1DA,0xEF},  //	;    MOV      A,R7
{0xA1DB,0xF0},  //	;    MOVX     @DPTR,A
{0xA1DC,0x90},  //	;    MOV      DPTR,#0x0ABD
{0xA1DD,0x0A},  //	; 
{0xA1DE,0xBD},  //	; 
{0xA1DF,0xE0},  //	;    MOVX     A,@DPTR
{0xA1E0,0x04},  //	;    INC      A
{0xA1E1,0xF0},  //	;    MOVX     @DPTR,A
{0xA1E2,0x70},  //	;    JNZ      B00:AC20
{0xA1E3,0x06},  //	; 
{0xA1E4,0x90},  //	;    MOV      DPTR,#0x0ABC
{0xA1E5,0x0A},  //	; 
{0xA1E6,0xBC},  //	; 
{0xA1E7,0xE0},  //	;    MOVX     A,@DPTR
{0xA1E8,0x04},  //	;    INC      A
{0xA1E9,0xF0},  //	;    MOVX     @DPTR,A
{0xA1EA,0x90},  //	;    MOV      DPTR,#0x0ABB
{0xA1EB,0x0A},  //	; 
{0xA1EC,0xBB},  //	; 
{0xA1ED,0xE0},  //	;    MOVX     A,@DPTR
{0xA1EE,0x24},  //	;    ADD      A,#0x03
{0xA1EF,0x03},  //	; 
{0xA1F0,0xF0},  //	;    MOVX     @DPTR,A
{0xA1F1,0x90},  //	;    MOV      DPTR,#0x0ABA
{0xA1F2,0x0A},  //	; 
{0xA1F3,0xBA},  //	; 
{0xA1F4,0xE0},  //	;    MOVX     A,@DPTR
{0xA1F5,0x34},  //	;    ADDC     A,#DeviceParameters(0x00)
{0xA1F6,0x00},  //	; 
{0xA1F7,0xF0},  //	;    MOVX     @DPTR,A
{0xA1F8,0x05},  //	;    INC      0x22
{0xA1F9,0x22},  //	; 
{0xA1FA,0x02},  //	;    LJMP     B00:A409
{0xA1FB,0xF1},  //	; 
{0xA1FC,0x41},  //	; 
{0xA1FD,0x90},  //	;    MOV      DPTR,#0x0ABA
{0xA1FE,0x0A},  //	; 
{0xA1FF,0xBA},  //	; 
{0xA200,0x74},  //	;    MOV      A,#g_fpPixelCount(0x0E)
{0xA201,0x0E},  //	; 
{0xA202,0xF0},  //	;    MOVX     @DPTR,A
{0xA203,0xA3},  //	;    INC      DPTR
{0xA204,0x74},  //	;    MOV      A,#0xDC
{0xA205,0xDC},  //	; 
{0xA206,0xF0},  //	;    MOVX     @DPTR,A
{0xA207,0xA3},  //	;    INC      DPTR
{0xA208,0x74},  //	;    MOV      A,#0x05
{0xA209,0x05},  //	; 
{0xA20A,0xF0},  //	;    MOVX     @DPTR,A
{0xA20B,0xA3},  //	;    INC      DPTR
{0xA20C,0x74},  //	;    MOV      A,#0x61
{0xA20D,0x61},  //	; 
{0xA20E,0xF0},  //	;    MOVX     @DPTR,A
{0xA20F,0x90},  //	;    MOV      DPTR,#0x0ABA
{0xA210,0x0A},  //	; 
{0xA211,0xBA},  //	; 
{0xA212,0xE0},  //	;    MOVX     A,@DPTR
{0xA213,0xFE},  //	;    MOV      R6,A
{0xA214,0xA3},  //	;    INC      DPTR
{0xA215,0xE0},  //	;    MOVX     A,@DPTR
{0xA216,0xAA},  //	;    MOV      R2,uwDelay100(0x06)
{0xA217,0x06},  //	; 
{0xA218,0xF9},  //	;    MOV      R1,A
{0xA219,0x7B},  //	;    MOV      R3,#0x01
{0xA21A,0x01},  //	; 
{0xA21B,0xC0},  //	;    PUSH     0x02
{0xA21C,0x02},  //	; 
{0xA21D,0xA3},  //	;    INC      DPTR
{0xA21E,0xE0},  //	;    MOVX     A,@DPTR
{0xA21F,0xFE},  //	;    MOV      R6,A
{0xA220,0xA3},  //	;    INC      DPTR
{0xA221,0xE0},  //	;    MOVX     A,@DPTR
{0xA222,0xAA},  //	;    MOV      R2,uwDelay100(0x06)
{0xA223,0x06},  //	; 
{0xA224,0xF8},  //	;    MOV      R0,A
{0xA225,0xAC},  //	;    MOV      R4 02
{0xA226,0x02},  //	; 
{0xA227,0x7D},  //	;    MOV      R5,#0x01
{0xA228,0x01},  //	; 
{0xA229,0xD0},  //	;    POP      0x02
{0xA22A,0x02},  //	; 
{0xA22B,0x7E},  //	;    MOV      R6,#DeviceParameters(0x00)
{0xA22C,0x00},  //	; 
{0xA22D,0x7F},  //	;    MOV      R7,#uwDelay1000(0x04)
{0xA22E,0x04},  //	; 
{0xA22F,0x12},  //	;    LCALL    C?COPY517(C:0F6F)
{0xA230,0x0F},  //	; 
{0xA231,0x6F},  //	; 
{0xA232,0x02},  //	;    JUMP    
{0xA233,0x66},  //	; 
{0xA234,0xD9},  //	; 
{0xA235,0x90},  //	;    MOV      R4 02
{0xA236,0x07},  //	; 
{0xA237,0xD0},  //	;    MOV      R5,#0x01
{0xA238,0x02},  //	; 
{0xA239,0xA2},  //	;    POP      0x02
{0xA23A,0x69},  //	; 
{0xA240,0x02},  //	;    ANL      A,#0xFD
{0xA241,0x21},  //	; 
{0xA242,0x7F},  //	;    MOVX     @DPTR,A
{0xA243,0x02},  //	;    ANL      A,#0xFD
{0xA244,0x21},  //	; 
{0xA245,0xF4},  //	;    MOVX     @DPTR,A
{0xA246,0x02},  //	;    ANL      A,#0xFD
{0xA247,0xA6},  //	; 
{0xA248,0x15},  //	;    MOVX     @DPTR,A
{0xA249,0x60},  //	;    JZ       C:29EF
{0xA24A,0x0A},  //	; 
{0xA24B,0xEF},  //	;    MOV      A,R7
{0xA24C,0xB4},  //	;    CJNE     A,#0x01,C:29FF
{0xA24D,0x01},  //	; 
{0xA24E,0x16},  //	; 
{0xA24F,0x90},  //	;    MOV      DPTR,#ModeSetup(0x005D)
{0xA250,0x00},  //	; 
{0xA251,0x5D},  //	; 
{0xA252,0xE0},  //	;    MOVX     A,@DPTR
{0xA253,0x70},  //	;    JNZ      C:29FF
{0xA254,0x10},  //	; 
{0xA255,0x12},  //	;    LCALL    StreamManager_ResumeStreaming(C:26C8)
{0xA256,0x26},  //	; 
{0xA257,0xC8},  //	; 
{0xA258,0x90},  //	;    MOV      DPTR,#0x0011
{0xA259,0x00},  //	; 
{0xA25A,0x11},  //	; 
{0xA25B,0x74},  //	;    MOV      A,#0x30
{0xA25C,0x30},  //	; 
{0xA25D,0xF0},  //	;    MOVX     @DPTR,A
{0xA25E,0x90},  //	;    MOV      DPTR,#fpHighClipForDesiredExposure(0x0010)
{0xA25F,0x00},  //	; 
{0xA260,0x10},  //	; 
{0xA261,0x74},  //	;    MOV      A,#0x01
{0xA262,0x01},  //	; 
{0xA263,0xF0},  //	;    MOVX     @DPTR,A
{0xA264,0x22},  //	;    RET      
{0xA265,0x12},  //	;    LCALL    C:25A8
{0xA266,0x25},  //	; 
{0xA267,0xA8},  //	; 
{0xA268,0x02},  //	;      RET     
{0xA269,0x29},  //	; 
{0xA26A,0xFC},  //	; 
{0xA26B,0x44},  //	;    ORL      A,#fpHighClipForDesiredExposure(0x10)
{0xA26C,0x18},  //	; 
{0xA26D,0xF0},  //	;    MOVX     @DPTR,A
{0xA26E,0x90},  //	;    MOV      DPTR,#Tx_Csi2_Dphy_Datalane2_Pwr_Ctrl(0x7218)
{0xA26F,0x72},  //	; 
{0xA270,0x18},  //	; 
{0xA271,0xE0},  //	;    MOVX     A,@DPTR
{0xA272,0x44},  //	;    ORL      A,#fpHighClipForDesiredExposure(0x10)
{0xA273,0x18},  //	; 
{0xA274,0xF0},  //	;    MOVX     @DPTR,A
{0xA275,0x00},  //	;    MOV      DPTR,#Tx_Csi2_Dphy_Clklane_Pwr_Ctrl(0x7208)
{0xA276,0x00},  //	; 
{0xA277,0x00},  //	; 
{0xA278,0x00},  //	;    MOVX     A,@DPTR
{0xA279,0x00},  //	;    ORL      A,#fpHighClipForDesiredExposure(0x10)
{0xA27A,0x00},  //	; 
{0xA27B,0x90},  //	;    MOVX     @DPTR,A
{0xA27C,0x72},  //	;    MOV      DPTR,#Tx_Csi2_Pwr_Ctrl(0x7214)
{0xA27D,0x08},  //	; 
{0xA27E,0xE0},  //	; 
{0xA27F,0x44},  //	;    MOVX     A,@DPTR
{0xA280,0x10},  //	;    ANL      A,#0xFD
{0xA281,0xF0},  //	; 
{0xA282,0x90},  //	;    MOVX     @DPTR,A
{0xA283,0x72},  //	;    MOV      DPTR,#Tx_Csi2_Dphy_Pwr_Ctrl(0x7204)
{0xA284,0x14},  //	; 
{0xA285,0xE0},  //	; 
{0xA286,0x54},  //	;    MOV      A,#0x1F
{0xA287,0xFD},  //	; 
{0xA288,0xF0},  //	;    MOVX     @DPTR,A
{0xA289,0x22},  //	;    RET     
{0xA29B,0xF0},  //	;    MOVX     @DPTR,A
{0xA29C,0xD3},  //	;    SETB     C
{0xA29D,0x90},  //	;    MOV      DPTR,#0x0791
{0xA29E,0x07},  //	; 
{0xA29F,0x91},  //	; 
{0xA2A0,0xE0},  //	;    MOVX     A,@DPTR
{0xA2A1,0x94},  //	;    SUBB     A,#0x21
{0xA2A2,0x21},  //	; 
{0xA2A3,0x90},  //	;    MOV      DPTR,#AutoFocusInput(0x0790)
{0xA2A4,0x07},  //	; 
{0xA2A5,0x90},  //	; 
{0xA2A6,0xE0},  //	;    MOVX     A,@DPTR
{0xA2A7,0x64},  //	;    XRL      A,#PipeSetupCommon(0x80)
{0xA2A8,0x80},  //	; 
{0xA2A9,0x94},  //	;    SUBB     A,#SP(0x81)
{0xA2AA,0x81},  //	; 
{0xA2AB,0x40},  //	;    JC       B01:B152
{0xA2AC,0x08},  //	; 
{0xA2AD,0x90},  //	;    MOV      DPTR,#0x07CB
{0xA2AE,0x07},  //	; 
{0xA2AF,0xCB},  //	; 
{0xA2B0,0x74},  //	;    MOV      A,#0xFF
{0xA2B1,0xFF},  //	; 
{0xA2B2,0xF0},  //	;    MOVX     @DPTR,A
{0xA2B3,0x80},  //	;    SJMP     B01:B158
{0xA2B4,0x06},  //	; 
{0xA2B5,0x90},  //	;    MOV      DPTR,#0x07CB
{0xA2B6,0x07},  //	; 
{0xA2B7,0xCB},  //	; 
{0xA2B8,0x74},  //	;    MOV      A,#0x01
{0xA2B9,0x01},  //	; 
{0xA2BA,0xF0},  //	;    MOVX     @DPTR,A
{0xA2BB,0x02},  //	;    JUMP
{0xA2BC,0xB5},  //	; 
{0xA2BD,0xC3},  //	; 
{0xA2BE,0x90},  //	;    MOV      DPTR,#0x0834
{0xA2BF,0x08},  //	; 
{0xA2C0,0x34},  //	; 
{0xA2C1,0xE0},  //	;    MOVX     A,@DPTR
{0xA2C2,0xFC},  //	;    MOV      R4,A
{0xA2C3,0xA3},  //	;    INC      DPTR
{0xA2C4,0xE0},  //	;    MOVX     A,@DPTR
{0xA2C5,0xFD},  //	;    MOV      R5,A
{0xA2C6,0xA3},  //	;    INC      DPTR
{0xA2C7,0xE0},  //	;    MOVX     A,@DPTR
{0xA2C8,0xFE},  //	;    MOV      R6,A
{0xA2C9,0xA3},  //	;    INC      DPTR
{0xA2CA,0xE0},  //	;    MOVX     A,@DPTR
{0xA2CB,0xFF},  //	;    MOV      R7,A
{0xA2CC,0x90},  //	;    MOV      DPTR,#AutoFocusMeasureData(0x07D0)
{0xA2CD,0x07},  //	; 
{0xA2CE,0xD0},  //	; 
{0xA2CF,0xE0},  //	;    MOVX     A,@DPTR
{0xA2D0,0xF8},  //	;    MOV      R0,A
{0xA2D1,0xA3},  //	;    INC      DPTR
{0xA2D2,0xE0},  //	;    MOVX     A,@DPTR
{0xA2D3,0xF9},  //	;    MOV      R1,A
{0xA2D4,0xA3},  //	;    INC      DPTR
{0xA2D5,0xE0},  //	;    MOVX     A,@DPTR
{0xA2D6,0xFA},  //	;    MOV      R2,A
{0xA2D7,0xA3},  //	;    INC      DPTR
{0xA2D8,0xE0},  //	;    MOVX     A,@DPTR
{0xA2D9,0xFB},  //	;    MOV      R3,A
{0xA2DA,0xD3},  //	;    SETB     C
{0xA2DB,0x12},  //	;    LCALL    C?ULCMP(C:0DAE)
{0xA2DC,0x0D},  //	; 
{0xA2DD,0xAE},  //	; 
{0xA2DE,0x40},  //	;    JC       B01:9FDA
{0xA2DF,0x0B},  //	; 
{0xA2E0,0x12},  //	;    LCALL    HCS_Initialization(B01:B0EF)
{0xA2E1,0xB5},  //	; 
{0xA2E2,0x49},  //	; 
{0xA2E3,0x90},  //	;    MOV      DPTR,#0x07A4
{0xA2E4,0x07},  //	; 
{0xA2E5,0xA4},  //	; 
{0xA2E6,0x74},  //	;    MOV      A,#0x02
{0xA2E7,0x02},  //	; 
{0xA2E8,0xF0},  //	;    MOVX     @DPTR,A
{0xA2E9,0x80},  //	;    SJMP     B01:9FE3
{0xA2EA,0x09},  //	; 
{0xA2EB,0x12},  //	;    LCALL    LowFocusMeasureFullSearchInit(B01:B7AE)
{0xA2EC,0xB7},  //	; 
{0xA2ED,0x51},  //	; 
{0xA2EE,0x90},  //	;    MOV      DPTR,#0x07A4
{0xA2EF,0x07},  //	; 
{0xA2F0,0xA4},  //	; 
{0xA2F1,0x74},  //	;    MOV      A,#0x05
{0xA2F2,0x05},  //	; 
{0xA2F3,0xF0},  //	;    MOVX     @DPTR,A
{0xA2F4,0x02},  //	;    JUM
{0xA2F5,0xA2},  //	; 
{0xA2F6,0xDA},  //	; 
{0xA2F7,0x90},  //	;    MOV      DPTR,#fOTPRed(0x0EE0)
{0xA2F8,0x0E},  //	; 
{0xA2F9,0xE0},  //	; 
{0xA2FA,0xE0},  //	;    MOVX     A,@DPTR
{0xA2FB,0xFD},  //	;    MOV      R5,A
{0xA2FC,0xA3},  //	;    INC      DPTR
{0xA2FD,0xE0},  //	;    MOVX     A,@DPTR
{0xA2FE,0x90},  //	;    MOV      DPTR,#0x02A2
{0xA2FF,0x02},  //	; 
{0xA300,0xA2},  //	; 
{0xA301,0xCD},  //	;    XCH      A,R5
{0xA302,0xF0},  //	;    MOVX     @DPTR,A
{0xA303,0xA3},  //	;    INC      DPTR
{0xA304,0xED},  //	;    MOV      A,R5
{0xA305,0xF0},  //	;    MOVX     @DPTR,A
{0xA306,0x90},  //	;    MOV      DPTR,#fOTPBlue(0x0EE2)
{0xA307,0x0E},  //	; 
{0xA308,0xE2},  //	; 
{0xA309,0xE0},  //	;    MOVX     A,@DPTR
{0xA30A,0xFD},  //	;    MOV      R5,A
{0xA30B,0xA3},  //	;    INC      DPTR
{0xA30C,0xE0},  //	;    MOVX     A,@DPTR
{0xA30D,0x90},  //	;    MOV      DPTR,#0x02A8
{0xA30E,0x02},  //	; 
{0xA30F,0xA8},  //	; 
{0xA310,0xCD},  //	;    XCH      A,R5
{0xA311,0xF0},  //	;    MOVX     @DPTR,A
{0xA312,0xA3},  //	;    INC      DPTR
{0xA313,0xED},  //	;    MOV      A,R5
{0xA314,0xF0},  //	;    MOVX     @DPTR,A
{0xA315,0xE4},  //	;    CLR      A
{0xA316,0x90},  //	;    MOV      DPTR,#PresetControl(0x0638)
{0xA317,0x06},  //	; 
{0xA318,0x38},  //	; 
{0xA319,0xF0},  //	;    MOVX     @DPTR,A
{0xA31A,0x02},  //	;    JUMP     #676
{0xA31B,0x67},  //	; 
{0xA31C,0x63},  //	; 
{0xA31D,0x90},  //	;    MOV      DPTR,#bDarkCalSR(0x0EE8)
{0xA31E,0x0E},  //	; 
{0xA31F,0xE8},  //	; 
{0xA320,0xE0},  //	;    MOVX     A,@DPTR
{0xA321,0x90},  //	;    MOV      DPTR,#0x0262
{0xA322,0x02},  //	; 
{0xA323,0x62},  //	; 
{0xA324,0xF0},  //	;    MOVX     @DPTR,A
{0xA325,0x90},  //	;    MOV      DPTR,#bDarkCalAB4(0x0EE9)
{0xA326,0x0E},  //	; 
{0xA327,0xE9},  //	; 
{0xA328,0xE0},  //	;    MOVX     A,@DPTR
{0xA329,0x90},  //	;    MOV      DPTR,#0x0263
{0xA32A,0x02},  //	; 
{0xA32B,0x63},  //	; 
{0xA32C,0xF0},  //	;    MOVX     @DPTR,A
{0xA32D,0x02},  //	;    JUMP     #676
{0xA32E,0x67},  //	; 
{0xA32F,0x1F},  //	; 
{0xA33B,0x90},  //	;    MOV      DPTR,#INFINITY_OTP(0xE014)
{0xA33C,0x0E},  //	; 
{0xA33D,0x14},  //	; 
{0xA33E,0xE0},  //	;    MOVX     A,@DPTR
{0xA33F,0xFE},  //	;    MOV      R6,A
{0xA340,0xA3},  //	;    INC      DPTR
{0xA341,0xE0},  //	;    MOVX     A,@DPTR
{0xA342,0xFF},  //	;    MOV      R7,A
{0xA343,0x90},  //	;    MOV      DPTR,#0x06D9
{0xA344,0x06},  //	; 
{0xA345,0xD9},  //	; 
{0xA346,0xEE},  //	;    MOV      A,R6
{0xA347,0xF0},  //	;    MOVX     @DPTR,A
{0xA348,0xA3},  //	;    INC      DPTR
{0xA349,0xEF},  //	;    MOV      A,R7
{0xA34A,0xF0},  //	;    MOVX     @DPTR,A
{0xA34B,0x90},  //	;    MOV      DPTR,#DELTA_UP_OTP(0xE018)
{0xA34C,0x0E},  //	; 
{0xA34D,0x18},  //	; 
{0xA34E,0xE0},  //	;    MOVX     A,@DPTR
{0xA34F,0xFD},  //	;    MOV      R5,A
{0xA350,0x7C},  //	;    MOV      R4,#DeviceParameters(0x00)
{0xA351,0x00},  //	; 
{0xA352,0xC3},  //	;    CLR      C
{0xA353,0xEF},  //	;    MOV      A,R7
{0xA354,0x9D},  //	;    SUBB     A,R5
{0xA355,0xEE},  //	;    MOV      A,R6
{0xA356,0x9C},  //	;    SUBB     A,R4
{0xA357,0x50},  //	;    JNC      C:2067
{0xA358,0x09},  //	; 
{0xA359,0xE4},  //	;    CLR      A
{0xA35A,0x90},  //	;    MOV      DPTR,#0x06D7
{0xA35B,0x06},  //	; 
{0xA35C,0xD7},  //	; 
{0xA35D,0xF0},  //	;    MOVX     @DPTR,A
{0xA35E,0xA3},  //	;    INC      DPTR
{0xA35F,0xF0},  //	;    MOVX     @DPTR,A
{0xA360,0x80},  //	;    SJMP     C:207A
{0xA361,0x13},  //	; 
{0xA362,0xC3},  //	;    CLR      C
{0xA363,0x90},  //	;    MOV      DPTR,#0x06DA
{0xA364,0x06},  //	; 
{0xA365,0xDA},  //	; 
{0xA366,0xE0},  //	;    MOVX     A,@DPTR
{0xA367,0x9D},  //	;    SUBB     A,R5
{0xA368,0xFE},  //	;    MOV      R6,A
{0xA369,0x90},  //	;    MOV      DPTR,#0x06D9
{0xA36A,0x06},  //	; 
{0xA36B,0xD9},  //	; 
{0xA36C,0xE0},  //	;    MOVX     A,@DPTR
{0xA36D,0x9C},  //	;    SUBB     A,R4
{0xA36E,0x90},  //	;    MOV      DPTR,#0x06D7
{0xA36F,0x06},  //	; 
{0xA370,0xD7},  //	; 
{0xA371,0xF0},  //	;    MOVX     @DPTR,A
{0xA372,0xA3},  //	;    INC      DPTR
{0xA373,0xCE},  //	;    XCH      A,R6
{0xA374,0xF0},  //	;    MOVX     @DPTR,A
{0xA375,0x90},  //	;    MOV      DPTR,#DELTA_UP_OTP(0xE018)
{0xA376,0x0E},  //	; 
{0xA377,0x18},  //	; 
{0xA378,0xE0},  //	;    MOVX     A,@DPTR
{0xA379,0xF9},  //	;    MOV      R1,A
{0xA37A,0xFF},  //	;    MOV      R7,A
{0xA37B,0x90},  //	;    MOV      DPTR,#0x06C2
{0xA37C,0x06},  //	; 
{0xA37D,0xC2},  //	; 
{0xA37E,0xE0},  //	;    MOVX     A,@DPTR
{0xA37F,0xFC},  //	;    MOV      R4,A
{0xA380,0xA3},  //	;    INC      DPTR
{0xA381,0xE0},  //	;    MOVX     A,@DPTR
{0xA382,0xFD},  //	;    MOV      R5,A
{0xA383,0xC3},  //	;    CLR      C
{0xA384,0x9F},  //	;    SUBB     A,R7
{0xA385,0xFF},  //	;    MOV      R7,A
{0xA386,0xEC},  //	;    MOV      A,R4
{0xA387,0x94},  //	;    SUBB     A,#DeviceParameters(0x00)
{0xA388,0x00},  //	; 
{0xA389,0xFE},  //	;    MOV      R6,A
{0xA38A,0x90},  //	;    MOV      DPTR,#MACRO_OTP(0xE016)
{0xA38B,0x0E},  //	; 
{0xA38C,0x16},  //	; 
{0xA38D,0xE0},  //	;    MOVX     A,@DPTR
{0xA38E,0xFA},  //	;    MOV      R2,A
{0xA38F,0xA3},  //	;    INC      DPTR
{0xA390,0xE0},  //	;    MOVX     A,@DPTR
{0xA391,0xFB},  //	;    MOV      R3,A
{0xA392,0xD3},  //	;    SETB     C
{0xA393,0x9F},  //	;    SUBB     A,R7
{0xA394,0xEA},  //	;    MOV      A,R2
{0xA395,0x9E},  //	;    SUBB     A,R6
{0xA396,0x40},  //	;    JC       C:20A7
{0xA397,0x0A},  //	; 
{0xA398,0x90},  //	;    MOV      DPTR,#0x06D5
{0xA399,0x06},  //	; 
{0xA39A,0xD5},  //	; 
{0xA39B,0xEC},  //	;    MOV      A,R4
{0xA39C,0xF0},  //	;    MOVX     @DPTR,A
{0xA39D,0xA3},  //	;    INC      DPTR
{0xA39E,0xED},  //	;    MOV      A,R5
{0xA39F,0xF0},  //	;    MOVX     @DPTR,A
{0xA3A0,0x80},  //	;    SJMP     C:20B5
{0xA3A1,0x0E},  //	; 
{0xA3A2,0xE9},  //	;    MOV      A,R1
{0xA3A3,0x7E},  //	;    MOV      R6,#DeviceParameters(0x00)
{0xA3A4,0x00},  //	; 
{0xA3A5,0x2B},  //	;    ADD      A,R3
{0xA3A6,0xFF},  //	;    MOV      R7,A
{0xA3A7,0xEE},  //	;    MOV      A,R6
{0xA3A8,0x3A},  //	;    ADDC     A,R2
{0xA3A9,0x90},  //	;    MOV      DPTR,#0x06D5
{0xA3AA,0x06},  //	; 
{0xA3AB,0xD5},  //	; 
{0xA3AC,0xF0},  //	;    MOVX     @DPTR,A
{0xA3AD,0xA3},  //	;    INC      DPTR
{0xA3AE,0xEF},  //	;    MOV      A,R7
{0xA3AF,0xF0},  //	;    MOVX     @DPTR,A
{0xA3B0,0xE9},  //	;    MOV      A,R1
{0xA3B1,0xFB},  //	;    MOV      R3,A
{0xA3B2,0x7A},  //	;    MOV      R2,#DeviceParameters(0x00)
{0xA3B3,0x00},  //	; 
{0xA3B4,0x90},  //	;    MOV      DPTR,#0xE015
{0xA3B5,0x0E},  //	; 
{0xA3B6,0x15},  //	; 
{0xA3B7,0xE0},  //	;    MOVX     A,@DPTR
{0xA3B8,0x2B},  //	;    ADD      A,R3
{0xA3B9,0xFE},  //	;    MOV      R6,A
{0xA3BA,0x90},  //	;    MOV      DPTR,#INFINITY_OTP(0xE014)
{0xA3BB,0x0E},  //	; 
{0xA3BC,0x14},  //	; 
{0xA3BD,0xE0},  //	;    MOVX     A,@DPTR
{0xA3BE,0x3A},  //	;    ADDC     A,R2
{0xA3BF,0x90},  //	;    MOV      DPTR,#0x06E1
{0xA3C0,0x06},  //	; 
{0xA3C1,0xE1},  //	; 
{0xA3C2,0xF0},  //	;    MOVX     @DPTR,A
{0xA3C3,0xA3},  //	;    INC      DPTR
{0xA3C4,0xCE},  //	;    XCH      A,R6
{0xA3C5,0xF0},  //	;    MOVX     @DPTR,A
{0xA3C6,0xC3},  //	;    CLR      C
{0xA3C7,0x90},  //	;    MOV      DPTR,#0xE017
{0xA3C8,0x0E},  //	; 
{0xA3C9,0x17},  //	; 
{0xA3CA,0xE0},  //	;    MOVX     A,@DPTR
{0xA3CB,0x9B},  //	;    SUBB     A,R3
{0xA3CC,0xFE},  //	;    MOV      R6,A
{0xA3CD,0x90},  //	;    MOV      DPTR,#MACRO_OTP(0xE016)
{0xA3CE,0x0E},  //	; 
{0xA3CF,0x16},  //	; 
{0xA3D0,0x02},  //	;    JUMP    
{0xA3D1,0x20},  //	; 
{0xA3D2,0xD5},  //	; 
{0xA3D3,0x90},  //	;    MOV      DPTR,#bDarkCalHFPN(0x0EE4)
{0xA3d4,0x0E},  //	; 
{0xA3d5,0xE4},  //	; 
{0xA3d6,0xE0},  //	;    MOVX     A,@DPTR
{0xA3d7,0x90},  //	;    MOV      DPTR,#0x0266
{0xA3d8,0x02},  //	; 
{0xA3d9,0x66},  //	; 
{0xA3da,0xF0},  //	;    MOVX     @DPTR,A
{0xA3DB,0x90},  //	;    MOV      DPTR,#bDarkCalHFPN(0x0EE4)
{0xA3dc,0x0E},  //	; 
{0xA3dd,0xE5},  //	; 
{0xA3de,0xE0},  //	;    MOVX     A,@DPTR
{0xA3df,0x90},  //	;    MOV      DPTR,#0x0266
{0xA3e0,0x02},  //	; 
{0xA3e1,0x64},  //	; 
{0xA3e2,0xF0},  //	;    MOVX     @DPTR,A
{0xA3e3,0x90},  //	;    MOV      DPTR,#bDarkCalHFPN(0x0EE4)
{0xA3e4,0x0E},  //	; 
{0xA3e5,0xE6},  //	; 
{0xA3e6,0xE0},  //	;    MOVX     A,@DPTR
{0xA3e7,0x90},  //	;    MOV      DPTR,#0x0266
{0xA3e8,0x02},  //	; 
{0xA3e9,0x65},  //	; 
{0xA3ea,0xF0},  //	;    MOVX     @DPTR,A
{0xA3eb,0x02},  //	;    JUMP    
{0xA3ec,0x67},  //	; 
{0xA3ed,0xA5},  //	; 
{0xA3f0,0x12},  //	; 
{0xA3f1,0x47},  //	; 
{0xA3f2,0x59},  //	;    MOVX     @DPTR,A
{0xA3f3,0x90},  //	;    MOV      DPTR,#bDarkCalHFPN(0x0EE4)
{0xA3f4,0x00},  //	; 
{0xA3f5,0xB5},  //	; 
{0xA3f6,0xE0},  //	;    MOVX     A,@DPTR
{0xA3f7,0xB4},  //	;    MOVX     A,@DPTR
{0xA3f8,0x02},  //	;    MOV      DPTR,#0x0266
{0xA3f9,0x03},  //	; 
{0xA3fa,0x12},  //	; 
{0xA3fb,0x47},  //	;    MOVX     @DPTR,A
{0xA3fc,0x59},  //	;    JUMP    
{0xA3fd,0x02},  //	; 
{0xA3fe,0xC5},  //	; 
{0xA3ff,0xC3},  //	; 
{0xA400,0x90},  //	;    MOV      DPTR,#c_HFlip(0x003D)
{0xA401,0x00},  //	; 
{0xA402,0x3D},  //	; 
{0xA403,0xF0},  //	;    MOVX     @DPTR,A
{0xA404,0x90},  //	;    MOV      DPTR,#0x0084
{0xA405,0x00},  //	; 
{0xA406,0x84},  //	; 
{0xA407,0xE0},  //	;    MOVX     A,@DPTR
{0xA408,0xFE},  //	;    MOV      R6,A
{0xA409,0x90},  //	;    MOV      DPTR,#c_VFlip(0x003E)
{0xA40A,0x00},  //	; 
{0xA40B,0x3E},  //	; 
{0xA40C,0xF0},  //	;    MOVX     @DPTR,A
{0xA40D,0xEF},  //	;    MOV      A,R7
{0xA40E,0x70},  //	;    JNZ      B00:8201
{0xA40F,0x03},  //	; 
{0xA410,0xEE},  //	;    MOV      A,R6
{0xA411,0x60},  //	;    JZ       B00:8205
{0xA412,0x04},  //	; 
{0xA413,0x7F},  //	;    MOV      R7,#0x01
{0xA414,0x01},  //	; 
{0xA415,0x80},  //	;    SJMP     B00:8207
{0xA416,0x02},  //	; 
{0xA417,0x7F},  //	;    MOV      R7,#DeviceParameters(0x00)
{0xA418,0x00},  //	; 
{0xA419,0x90},  //	;    MOV      DPTR,#c_HVFlip(0x003F)
{0xA41A,0x00},  //	; 
{0xA41B,0x3F},  //	; 
{0xA41C,0xEF},  //	;    MOV      A,R7
{0xA41D,0xF0},  //	;    MOVX     @DPTR,A
{0xA41E,0x02},  //	;    JUMP bac
{0xA41F,0x89},  //	; 
{0xA420,0xD3},  //	; 
{0xA421,0x90},  //	;    MOV      DPTR,#uwI2CSIndex(0x0012)
{0xA422,0x00},  //	; 
{0xA423,0x12},  //	; 
{0xA424,0xE0},  //	;    MOVX     A,@DPTR
{0xA425,0xFF},  //	;    MOV      R7,A
{0xA426,0x70},  //	;    JNZ      B00:9AC3
{0xA427,0x0C},  //	; 
{0xA428,0x90},  //	;    MOV      DPTR,#0x0046
{0xA429,0x00},  //	; 
{0xA42A,0x46},  //	; 
{0xA42B,0xE0},  //	;    MOVX     A,@DPTR
{0xA42C,0xC3},  //	;    CLR      C
{0xA42D,0x94},  //	;    SUBB     A,#0x07
{0xA42E,0x07},  //	; 
{0xA42F,0x40},  //	;    JC       B00:9AC3
{0xA430,0x03},  //	; 
{0xA431,0x75},  //	;    MOV      0x2E,#0x02
{0xA432,0x2E},  //	; 
{0xA433,0x02},  //	; 
{0xA434,0xEF},  //	;    MOV      A,R7
{0xA435,0xB4},  //	;    CJNE     A,#0x01,B00:9AD3
{0xA436,0x01},  //	; 
{0xA437,0x0C},  //	; 
{0xA438,0x90},  //	;    MOV      DPTR,#XDroop_Reverse_Croping(0x0066)
{0xA439,0x00},  //	; 
{0xA43A,0x66},  //	; 
{0xA43B,0xE0},  //	;    MOVX     A,@DPTR
{0xA43C,0xC3},  //	;    CLR      C
{0xA43D,0x94},  //	;    SUBB     A,#0x07
{0xA43E,0x07},  //	; 
{0xA43F,0x40},  //	;    JC       B00:9AD3
{0xA440,0x03},  //	; 
{0xA441,0x75},  //	;    MOV      0x2E,#0x02
{0xA442,0x2E},  //	; 
{0xA443,0x02},  //	; 
{0xA444,0x02},  //	;    JUMP   
{0xA445,0xA7},  //	; 
{0xA446,0x9E},  //	; 
{0xA447,0xC3},  //	;   CLR      C
{0xA448,0x90},  //	;   MOV      DPTR,#0x0B8F
{0xA449,0x0B},  //	;
{0xA44A,0x8F},  //	;
{0xA44B,0xE0},  //	;   MOVX     A,@DPTR
{0xA44C,0x94},  //	;   SUBB     A,#PipeSetupCommon(0x80)
{0xA44D,0x80},  //	;
{0xA44E,0x90},  //	;   MOV      DPTR,#CalculateNormalisedStatistics?BYTE(0x0B8E)
{0xA44F,0x0B},  //	;
{0xA450,0x8E},  //	;
{0xA451,0xE0},  //	;   MOVX     A,@DPTR
{0xA452,0x94},  //	;   SUBB     A,#0x44
{0xA453,0x44},  //	;
{0xA454,0x40},  //	;   JC       B00:827D
{0xA455,0x22},  //	;
{0xA456,0x90},  //	;   MOV      DPTR,#0x0B91
{0xA457,0x0B},  //	;
{0xA458,0x91},  //	;
{0xA459,0xE0},  //	;   MOVX     A,@DPTR
{0xA45A,0x94},  //	;   SUBB     A,#PipeSetupCommon(0x80)
{0xA45B,0x80},  //	;
{0xA45C,0x90},  //	;   MOV      DPTR,#0x0B90
{0xA45D,0x0B},  //	;
{0xA45E,0x90},  //	;
{0xA45F,0xE0},  //	;   MOVX     A,@DPTR
{0xA460,0x94},  //	;   SUBB     A,#0x44
{0xA461,0x44},  //	;
{0xA462,0x40},  //	;   JC       B00:827D
{0xA463,0x14},  //	;
{0xA464,0x90},  //	;   MOV      DPTR,#0x0B93
{0xA465,0x0B},  //	;
{0xA466,0x93},  //	;
{0xA467,0xE0},  //	;   MOVX     A,@DPTR
{0xA468,0x94},  //	;   SUBB     A,#PipeSetupCommon(0x80)
{0xA469,0x80},  //	;
{0xA46A,0x90},  //	;   MOV      DPTR,#0x0B92
{0xA46B,0x0B},  //	;
{0xA46C,0x92},  //	;
{0xA46D,0xE0},  //	;   MOVX     A,@DPTR
{0xA46E,0x94},  //	;   SUBB     A,#0x44
{0xA46F,0x44},  //	;
{0xA470,0x40},  //	;   JC       B00:827D
{0xA471,0x06},  //	;
{0xA472,0x90},  //	;   MOV      DPTR,#0x01A4
{0xA473,0x01},  //	;
{0xA474,0xA4},  //	;
{0xA475,0x02},  //	;   LJMP     back
{0xA476,0x86},  //	;
{0xA477,0x57},  //	;
{0xA478,0x02},  //	;   LJMP     back
{0xA479,0x86},  //	;
{0xA47A,0x5C},  //	;                                                               
{0xA500,0xF5},  //     ;    MOV      c_HeightScale(0x3B),A                              
{0xA501,0x3B},  //     ;                                                                
{0xA502,0x90},  //     ;    MOV      DPTR,#0x066C                                       
{0xA503,0x06},  //     ;                                                                
{0xA504,0x6C},  //     ;                                                                
{0xA505,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA506,0xFF},  //     ;    MOV      R7,A                                               
{0xA507,0xE5},  //     ;    MOV      A,c_HeightScale(0x3B)                              
{0xA508,0x3B},  //     ;                                                                
{0xA509,0xC3},  //     ;    CLR      C                                                  
{0xA50A,0x9F},  //     ;    SUBB     A,R7                                               
{0xA50B,0x40},  //     ;    JC       B01:98E8                                           
{0xA50C,0x03},  //     ;                                                                
{0xA50D,0x02},  //     ;    LJMP     B01:99E6                                           
{0xA50E,0xF6},  //     ;                                                                
{0xA50F,0x0E},  //     ;                                                                
{0xA510,0x90},  //     ;    MOV      DPTR,#0x0BC6                                       
{0xA511,0x0B},  //     ;                                                                
{0xA512,0xC6},  //     ;                                                                
{0xA513,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA514,0x14},  //     ;    DEC      A                                                  
{0xA515,0x60},  //     ;    JZ       B01:992B                                           
{0xA516,0x3C},  //     ;                                                                
{0xA517,0x14},  //     ;    DEC      A                                                  
{0xA518,0x60},  //     ;    JZ       B01:995D                                           
{0xA519,0x6B},  //     ;                                                                
{0xA51A,0x24},  //     ;    ADD      A,#0x02                                            
{0xA51B,0x02},  //     ;                                                                
{0xA51C,0x60},  //     ;    JZ       B01:98F9                                           
{0xA51D,0x03},  //     ;                                                                
{0xA51E,0x02},  //     ;    LJMP     B01:998D                                           
{0xA51F,0xF5},  //     ;                                                                
{0xA520,0xB5},  //     ;                                                                
{0xA521,0x90},  //     ;    MOV      DPTR,#AutoFocusInstableFocusMeasureValues(0x0A9A)  
{0xA522,0x0A},  //     ;                                                                
{0xA523,0x9A},  //     ;                                                                
{0xA524,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA525,0xFB},  //     ;    MOV      R3,A                                               
{0xA526,0xA3},  //     ;    INC      DPTR                                               
{0xA527,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA528,0xFA},  //     ;    MOV      R2,A                                               
{0xA529,0xA3},  //     ;    INC      DPTR                                               
{0xA52A,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA52B,0xF9},  //     ;    MOV      R1,A                                               
{0xA52C,0x85},  //     ;    MOV      DPL(0x82),c_HeightScale(0x3B)                      
{0xA52D,0x3B},  //     ;                                                                
{0xA52E,0x82},  //     ;                                                                
{0xA52F,0x75},  //     ;    MOV      DPH(0x83),#DeviceParameters(0x00)                  
{0xA530,0x83},  //     ;                                                                
{0xA531,0x00},  //     ;                                                                
{0xA532,0x12},  //     ;    LCALL    C?CLDOPTR(C:0AB8)                                  
{0xA533,0x0A},  //     ;                                                                
{0xA534,0xB8},  //     ;                                                                
{0xA535,0xFF},  //     ;    MOV      R7,A                                               
{0xA536,0x74},  //     ;    MOV      A,#0xAB                                            
{0xA537,0xAB},  //     ;                                                                
{0xA538,0x25},  //     ;    ADD      A,c_HeightScale(0x3B)                              
{0xA539,0x3B},  //     ;                                                                
{0xA53A,0xF5},  //     ;    MOV      DPL(0x82),A                                        
{0xA53B,0x82},  //     ;                                                                
{0xA53C,0xE4},  //     ;    CLR      A                                                  
{0xA53D,0x34},  //     ;    ADDC     A,#bInt_Event_Status(0x0A)                         
{0xA53E,0x0A},  //     ;                                                                
{0xA53F,0xF5},  //     ;    MOV      DPH(0x83),A                                        
{0xA540,0x83},  //     ;                                                                
{0xA541,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA542,0xFD},  //     ;    MOV      R5,A                                               
{0xA543,0xC3},  //     ;    CLR      C                                                  
{0xA544,0xEF},  //     ;    MOV      A,R7                                               
{0xA545,0x9D},  //     ;    SUBB     A,R5                                               
{0xA546,0xFE},  //     ;    MOV      R6,A                                               
{0xA547,0xE4},  //     ;    CLR      A                                                  
{0xA548,0x94},  //     ;    SUBB     A,#DeviceParameters(0x00)                          
{0xA549,0x00},  //     ;                                                                
{0xA54A,0x90},  //     ;    MOV      DPTR,#0x0BCA                                       
{0xA54B,0x0B},  //     ;                                                                
{0xA54C,0xCA},  //     ;                                                                
{0xA54D,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA54E,0xA3},  //     ;    INC      DPTR                                               
{0xA54F,0xCE},  //     ;    XCH      A,R6                                               
{0xA550,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA551,0x80},  //     ;    SJMP     B01:998D                                           
{0xA552,0x62},  //     ;                                                                
{0xA553,0x90},  //     ;    MOV      DPTR,#AutoFocusInstableFocusMeasureValues(0x0A9A)  
{0xA554,0x0A},  //     ;                                                                
{0xA555,0x9A},  //     ;                                                                
{0xA556,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA557,0xFB},  //     ;    MOV      R3,A                                               
{0xA558,0xA3},  //     ;    INC      DPTR                                               
{0xA559,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA55A,0xFA},  //     ;    MOV      R2,A                                               
{0xA55B,0xA3},  //     ;    INC      DPTR                                               
{0xA55C,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA55D,0xF9},  //     ;    MOV      R1,A                                               
{0xA55E,0x85},  //     ;    MOV      DPL(0x82),c_HeightScale(0x3B)                      
{0xA55F,0x3B},  //     ;                                                                
{0xA560,0x82},  //     ;                                                                
{0xA561,0x75},  //     ;    MOV      DPH(0x83),#DeviceParameters(0x00)                  
{0xA562,0x83},  //     ;                                                                
{0xA563,0x00},  //     ;                                                                
{0xA564,0x12},  //     ;    LCALL    C?CLDOPTR(C:0AB8)                                  
{0xA565,0x0A},  //     ;                                                                
{0xA566,0xB8},  //     ;                                                                
{0xA567,0xFF},  //     ;    MOV      R7,A                                               
{0xA568,0x74},  //     ;    MOV      A,#0x9D                                            
{0xA569,0x9D},  //     ;                                                                
{0xA56A,0x25},  //     ;    ADD      A,c_HeightScale(0x3B)                              
{0xA56B,0x3B},  //     ;                                                                
{0xA56C,0xF5},  //     ;    MOV      DPL(0x82),A                                        
{0xA56D,0x82},  //     ;                                                                
{0xA56E,0xE4},  //     ;    CLR      A                                                  
{0xA56F,0x34},  //     ;    ADDC     A,#bInt_Event_Status(0x0A)                         
{0xA570,0x0A},  //     ;                                                                
{0xA571,0xF5},  //     ;    MOV      DPH(0x83),A                                        
{0xA572,0x83},  //     ;                                                                
{0xA573,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA574,0xFD},  //     ;    MOV      R5,A                                               
{0xA575,0xC3},  //     ;    CLR      C                                                  
{0xA576,0xEF},  //     ;    MOV      A,R7                                               
{0xA577,0x9D},  //     ;    SUBB     A,R5                                               
{0xA578,0xFE},  //     ;    MOV      R6,A                                               
{0xA579,0xE4},  //     ;    CLR      A                                                  
{0xA57A,0x94},  //     ;    SUBB     A,#DeviceParameters(0x00)                          
{0xA57B,0x00},  //     ;                                                                
{0xA57C,0x90},  //     ;    MOV      DPTR,#0x0BCA                                       
{0xA57D,0x0B},  //     ;                                                                
{0xA57E,0xCA},  //     ;                                                                
{0xA57F,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA580,0xA3},  //     ;    INC      DPTR                                               
{0xA581,0xCE},  //     ;    XCH      A,R6                                               
{0xA582,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA583,0x80},  //     ;    SJMP     B01:998D                                           
{0xA584,0x30},  //     ;                                                                
{0xA585,0x90},  //     ;    MOV      DPTR,#AutoFocusInstableFocusMeasureValues(0x0A9A)  
{0xA586,0x0A},  //     ;                                                                
{0xA587,0x9A},  //     ;                                                                
{0xA588,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA589,0xFB},  //     ;    MOV      R3,A                                               
{0xA58A,0xA3},  //     ;    INC      DPTR                                               
{0xA58B,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA58C,0xFA},  //     ;    MOV      R2,A                                               
{0xA58D,0xA3},  //     ;    INC      DPTR                                               
{0xA58E,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA58F,0xF9},  //     ;    MOV      R1,A                                               
{0xA590,0x85},  //     ;    MOV      DPL(0x82),c_HeightScale(0x3B)                      
{0xA591,0x3B},  //     ;                                                                
{0xA592,0x82},  //     ;                                                                
{0xA593,0x75},  //     ;    MOV      DPH(0x83),#DeviceParameters(0x00)                  
{0xA594,0x83},  //     ;                                                                
{0xA595,0x00},  //     ;                                                                
{0xA596,0x12},  //     ;    LCALL    C?CLDOPTR(C:0AB8)                                  
{0xA597,0x0A},  //     ;                                                                
{0xA598,0xB8},  //     ;                                                                
{0xA599,0xFF},  //     ;    MOV      R7,A                                               
{0xA59A,0x74},  //     ;    MOV      A,#0xA4                                            
{0xA59B,0xA4},  //     ;                                                                
{0xA59C,0x25},  //     ;    ADD      A,c_HeightScale(0x3B)                              
{0xA59D,0x3B},  //     ;                                                                
{0xA59E,0xF5},  //     ;    MOV      DPL(0x82),A                                        
{0xA59F,0x82},  //     ;                                                                
{0xA5A0,0xE4},  //     ;    CLR      A                                                  
{0xA5A1,0x34},  //     ;    ADDC     A,#bInt_Event_Status(0x0A)                         
{0xA5A2,0x0A},  //     ;                                                                
{0xA5A3,0xF5},  //     ;    MOV      DPH(0x83),A                                        
{0xA5A4,0x83},  //     ;                                                                
{0xA5A5,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5A6,0xFD},  //     ;    MOV      R5,A                                               
{0xA5A7,0xC3},  //     ;    CLR      C                                                  
{0xA5A8,0xEF},  //     ;    MOV      A,R7                                               
{0xA5A9,0x9D},  //     ;    SUBB     A,R5                                               
{0xA5AA,0xFE},  //     ;    MOV      R6,A                                               
{0xA5AB,0xE4},  //     ;    CLR      A                                                  
{0xA5AC,0x94},  //     ;    SUBB     A,#DeviceParameters(0x00)                          
{0xA5AD,0x00},  //     ;                                                                
{0xA5AE,0x90},  //     ;    MOV      DPTR,#0x0BCA                                       
{0xA5AF,0x0B},  //     ;                                                                
{0xA5B0,0xCA},  //     ;                                                                
{0xA5B1,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA5B2,0xA3},  //     ;    INC      DPTR                                               
{0xA5B3,0xCE},  //     ;    XCH      A,R6                                               
{0xA5B4,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA5B5,0x90},  //     ;    MOV      DPTR,#0x0783                                       
{0xA5B6,0x07},  //     ;                                                                
{0xA5B7,0x83},  //     ;                                                                
{0xA5B8,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5B9,0xFF},  //     ;    MOV      R7,A                                               
{0xA5BA,0x7E},  //     ;    MOV      R6,#DeviceParameters(0x00)                         
{0xA5BB,0x00},  //     ;                                                                
{0xA5BC,0x90},  //     ;    MOV      DPTR,#patch_wLightGap(0x0DF6)                      
{0xA5BD,0x0D},  //     ;                                                                
{0xA5BE,0xF6},  //     ;                                                                
{0xA5BF,0xEE},  //     ;    MOV      A,R6                                               
{0xA5C0,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA5C1,0xA3},  //     ;    INC      DPTR                                               
{0xA5C2,0xEF},  //     ;    MOV      A,R7                                               
{0xA5C3,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA5C4,0x90},  //     ;    MOV      DPTR,#0x0BCA                                       
{0xA5C5,0x0B},  //     ;                                                                
{0xA5C6,0xCA},  //     ;                                                                
{0xA5C7,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5C8,0xFC},  //     ;    MOV      R4,A                                               
{0xA5C9,0xA3},  //     ;    INC      DPTR                                               
{0xA5CA,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5CB,0xFD},  //     ;    MOV      R5,A                                               
{0xA5CC,0xD3},  //     ;    SETB     C                                                  
{0xA5CD,0x9F},  //     ;    SUBB     A,R7                                               
{0xA5CE,0x74},  //     ;    MOV      A,#PipeSetupCommon(0x80)                           
{0xA5CF,0x80},  //     ;                                                                
{0xA5D0,0xF8},  //     ;    MOV      R0,A                                               
{0xA5D1,0xEC},  //     ;    MOV      A,R4                                               
{0xA5D2,0x64},  //     ;    XRL      A,#PipeSetupCommon(0x80)                           
{0xA5D3,0x80},  //     ;                                                                
{0xA5D4,0x98},  //     ;    SUBB     A,R0                                               
{0xA5D5,0x40},  //     ;    JC       B01:99BB                                           
{0xA5D6,0x0C},  //     ;                                                                
{0xA5D7,0x90},  //     ;    MOV      DPTR,#0x0BC8                                       
{0xA5D8,0x0B},  //     ;                                                                
{0xA5D9,0xC8},  //     ;                                                                
{0xA5DA,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5DB,0x04},  //     ;    INC      A                                                  
{0xA5DC,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA5DD,0xA3},  //     ;    INC      DPTR                                               
{0xA5DE,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5DF,0x04},  //     ;    INC      A                                                  
{0xA5E0,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA5E1,0x80},  //     ;    SJMP     B01:99E1                                           
{0xA5E2,0x26},  //     ;                                                                
{0xA5E3,0x90},  //     ;    MOV      DPTR,#patch_wLightGap(0x0DF6)                      
{0xA5E4,0x0D},  //     ;                                                                
{0xA5E5,0xF6},  //     ;                                                                
{0xA5E6,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5E7,0xFE},  //     ;    MOV      R6,A                                               
{0xA5E8,0xA3},  //     ;    INC      DPTR                                               
{0xA5E9,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA5EA,0xFF},  //     ;    MOV      R7,A                                               
{0xA5EB,0xC3},  //     ;    CLR      C                                                  
{0xA5EC,0xE4},  //     ;    CLR      A                                                  
{0xA5ED,0x9F},  //     ;    SUBB     A,R7                                               
{0xA5EE,0xFF},  //     ;    MOV      R7,A                                               
{0xA5EF,0xE4},  //     ;    CLR      A                                                  
{0xA5F0,0x9E},  //     ;    SUBB     A,R6                                               
{0xA5F1,0xFE},  //     ;    MOV      R6,A                                               
{0xA5F2,0xC3},  //     ;    CLR      C                                                  
{0xA5F3,0xED},  //     ;    MOV      A,R5                                               
{0xA5F4,0x9F},  //     ;    SUBB     A,R7                                               
{0xA5F5,0xEE},  //     ;    MOV      A,R6                                               
{0xA5F6,0x64},  //     ;    XRL      A,#PipeSetupCommon(0x80)                           
{0xA5F7,0x80},  //     ;                                                                
{0xA5F8,0xF8},  //     ;    MOV      R0,A                                               
{0xA5F9,0xEC},  //     ;    MOV      A,R4                                               
{0xA5FA,0x64},  //     ;    XRL      A,#PipeSetupCommon(0x80)                           
{0xA5FB,0x80},  //     ;                                                                
{0xA5FC,0x98},  //     ;    SUBB     A,R0                                               
{0xA5FD,0x50},  //     ;    JNC      B01:99E1                                           
{0xA5FE,0x0A},  //     ;                                                                
{0xA5FF,0x90},  //     ;    MOV      DPTR,#0x0BC8                                       
{0xA600,0x0B},  //     ;                                                                
{0xA601,0xC8},  //     ;                                                                
{0xA602,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA603,0x14},  //     ;    DEC      A                                                  
{0xA604,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA605,0xA3},  //     ;    INC      DPTR                                               
{0xA606,0xE0},  //     ;    MOVX     A,@DPTR                                            
{0xA607,0x04},  //     ;    INC      A                                                  
{0xA608,0xF0},  //     ;    MOVX     @DPTR,A                                            
{0xA609,0x05},  //     ;    INC      c_HeightScale(0x3B)                                
{0xA60A,0x3B},  //     ;                                                                
{0xA60B,0x02},  //     ;    LJMP     B01:98DA                                           
{0xA60C,0xF5},  //     ;                                                                
{0xA60D,0x02},  //     ;                                                                
{0xA60E,0x90},  //     ;    MOV      DPTR,#AutoFocusInstableFocusMeasureStatus(0x0858)  
{0xA60F,0x08},  //     ;                                                                
{0xA610,0x58},  //     ;                                                                
{0xA611,0x02},  //     ;    LJMP                                                        
{0xA612,0x9D},  //     ;                                                                
{0xA613,0x50},  //     ;                                                                
{0x9006,0xBA},  //	; Patch break point address high byte;
{0x9007,0x75},  //	; Patch break point address low byte;
{0x9008,0x00},  //	; Offset High byte;
{0x9009,0x00},  //	; Offset Low byte;
{0x900A,0x02},  //	; Enable BP 0;
{0x900D,0x01},  //	; Patch break point address bank;
{0x900E,0xA2},  //	; Patch break point address high byte;
{0x900F,0x8F},  //	; Patch break point address low byte;
{0x9010,0x00},  //	; Offset High byte;
{0x9011,0xCB},  //	; Offset Low byte;
{0x9012,0x03},  //	; Enable BP 1;
{0x9016,0xE6},  //	; Patch break point address high byte;
{0x9017,0x6B},  //	; Patch break point address low byte;
{0x9018,0x02},  //	; Offset High byte;
{0x9019,0x6B},  //	; Offset Low byte;
{0x901A,0x02},  //	; Enable BP 2;
{0x901D,0x01},  //    ;	
{0x901E,0xAC},  //	; Patch break point address high byte;
{0x901F,0x70},  //	; Patch break point address low byte;
{0x9020,0x00},  //	; Offset High byte;
{0x9021,0xC5},  //	; Offset Low byte;
{0x9022,0x03},  //	; Enable BP 3;
{0x9026,0x9C},  //	; Patch break point address high byte;
{0x9027,0x5B},  //	; Patch break point address low byte;
{0x9028,0x00},  //	; Offset High byte;
{0x9029,0xBF},  //	; Offset Low byte;
{0x902A,0x02},  //	; Enable BP 4;
{0x902E,0x60},  //	; Patch break point address high byte;
{0x902F,0x1C},  //	; Patch break point address low byte;
{0x9030,0x01},  //	; Offset High byte;
{0x9031,0x37},  //	; Offset Low byte;
{0x9032,0x02},  //	; Enable BP 3;
{0x9035,0x01},  //	; Patch break point address bank;
{0x9036,0xBA},  //	; Patch break point address high byte;
{0x9037,0x70},  //	; Patch break point address low byte;
{0x9038,0x00},  //	; Offset High byte;
{0x9039,0x00},  //	; Offset Low byte;
{0x903A,0x03},  //	; Enable BP 6;
{0x903E,0x21},  //	; Patch break point address high byte;
{0x903F,0x3F},  //	; Patch break point address low byte;
{0x9040,0x02},  //	; Offset High byte;
{0x9041,0x40},  //	; Offset Low byte;
{0x9042,0x02},  //	; Enable BP 7;
{0x9046,0x21},  //	; Patch break point address high byte;
{0x9047,0xEA},  //	; Patch break point address low byte;
{0x9048,0x02},  //	; Offset High byte;
{0x9049,0x43},  //	; Offset Low byte;
{0x904A,0x02},  //	; Enable BP 8;
{0x904E,0xA6},  //	; Patch break point address high byte;
{0x904F,0x12},  //	; Patch break point address low byte;
{0x9050,0x02},  //	; Offset High byte;
{0x9051,0x46},  //	; Offset Low byte;
{0x9052,0x02},  //	; Enable BP 9;
{0x9056,0x29},  //	; Patch break point address high byte;
{0x9057,0xE3},  //	; Patch break point address low byte;
{0x9058,0x02},  //	; Offset High byte;
{0x9059,0x49},  //	; Offset Low byte;
{0x905A,0x02},  //	; Enable BP 10;
{0x905D,0x01},  //	; Patch break point address bank;
{0x905E,0x9C},  //	; Patch break point address high byte;
{0x905F,0x6E},  //	; Patch break point address low byte;
{0x9060,0x05},  //	; Offset High byte;
{0x9061,0x00},  //	; Offset Low byte;
{0x9062,0x02},  //	; Enable BP 11;
{0x9065,0x01},  //	; Patch break point address bank;
{0x9066,0xA2},  //	; Patch break point address high byte;
{0x9067,0x66},  //	; Patch break point address low byte;
{0x9068,0x02},  //	; Offset High byte;
{0x9069,0x35},  //	; Offset Low byte;
{0x906A,0x02},  //	; Enable BP 12;
{0x906D,0x01},  //	; Patch break point address bank;
{0x906E,0xB5},  //	; Patch break point address high byte;
{0x906F,0xC2},  //	; Patch break point address low byte;
{0x9070,0x02},  //	; Offset High byte;
{0x9071,0x9B},  //	; Offset Low byte;
{0x9072,0x02},  //	; Enable BP 13;
{0x9075,0x01},  //	; Patch break point address bank;
{0x9076,0xA2},  //	; Patch break point address high byte;
{0x9077,0xD4},  //	; Patch break point address low byte;
{0x9078,0x02},  //	; Offset High byte;
{0x9079,0xBE},  //	; Offset Low byte;
{0x907A,0x02},  //	; Enable BP 14;
{0x907D,0x01},  //	; Patch break point address bank;
{0x907E,0xB7},  //	; Patch break point address high byte;
{0x907F,0xEA},  //	; Patch break point address low byte;
{0x9080,0x00},  //	; Offset High byte;
{0x9081,0x02},  //	; Offset Low byte;
{0x9082,0x03},  //	; Enable BP 15;
{0x9086,0x67},  //	; Patch break point address high byte;
{0x9087,0x31},  //	; Patch break point address low byte;
{0x9088,0x02},  //	; Offset High byte;
{0x9089,0xF7},  //	; Offset Low byte;
{0x908A,0x02},  //	; Enable BP 16;
{0x908E,0x66},  //	; Patch break point address high byte;
{0x908F,0xED},  //	; Patch break point address low byte;
{0x9090,0x03},  //	; Offset High byte;
{0x9091,0x1D},  //	; Offset Low byte;
{0x9092,0x02},  //	; Enable BP 17;
{0x9096,0x67},  //	; Patch break point address high byte;
{0x9097,0x73},  //	; Patch break point address low byte;
{0x9098,0x03},  //	; Offset High byte;
{0x9099,0xD3},  //	; Offset Low byte;
{0x909A,0x02},  //	; Enable BP 18;
{0x909E,0x20},  //	; Patch break point address high byte;
{0x909F,0x40},  //	; Patch break point address low byte;
{0x90A0,0x03},  //	; Offset High byte;
{0x90A1,0x3B},  //	; Offset Low byte;
{0x90A2,0x02},  //	; Enable BP 19;
{0x90A6,0xC5},  //	; Patch break point address high byte;
{0x90A7,0xC0},  //	; Patch break point address low byte;
{0x90A8,0x03},  //	; Offset High byte;
{0x90A9,0xF0},  //	; Offset Low byte;
{0x90AA,0x02},  //	; Enable BP 20;
{0x90AE,0x41},  //	; Patch break point address high byte;
{0x90AF,0xB3},  //	; Patch break point address low byte;
{0x90B0,0x00},  //	; Offset High byte;
{0x90B1,0xA2},  //	; Offset Low byte;
{0x90B2,0x02},  //	; Enable BP 21;
{0x90B6,0x44},  //	; Patch break point address high byte;
{0x90B7,0xBA},  //	; Patch break point address low byte;
{0x90B8,0x00},  //	; Offset High byte;
{0x90B9,0xF0},  //	; Offset Low byte;
{0x90BA,0x03},  //	; Enable BP 22;
{0x90BE,0x89},  //	; Patch break point address high byte;
{0x90BF,0x99},  //	; Patch break point address low byte;
{0x90C0,0x04},  //	; Offset High byte;
{0x90C1,0x00},  //	; Offset Low byte;
{0x90C2,0x02},  //	; Enable BP 23;
{0x90C6,0xA7},  //	; Patch break point address high byte;
{0x90C7,0x91},  //	; Patch break point address low byte;
{0x90C8,0x04},  //	; Offset High byte;
{0x90C9,0x21},  //	; Offset Low byte;
{0x90CA,0x02},  //	; Enable BP 24;
{0x90CE,0x3A},  //   ; Patch break point address high byte;
{0x90CF,0x51},  //   ; Patch break point address low byte;
{0x90D0,0x00},  //   ; Offset High byte;
{0x90D1,0xA2},  //   ; Offset Low byte;
{0x90D2,0x02},  //   ; Enable BP 25;
{0x90D6,0x86},  //    ; Patch break point address high byte;
{0x90D7,0x54},  //    ; Patch break point address low byte; 
{0x90D8,0x04},  //    ; Offset High byte;                   
{0x90D9,0x47},  //    ; Offset Low byte;                    
{0x90DA,0x02},  //    ; Enable BP 26;                       
{0x9000,0x01},  //	; Enable patch;
{0xffff,0x00},  //	; MCU release;delay200ms
{0x0010,0x00},//delay 200ms
{0x0009,0x16},  //
{0x0012,0x00},  //
{0x0013,0x00},  //
{0x0016,0x00},  //
{0x0021,0x00},  //
{0x0022,0x01},  //
{0x0040,0x01},  // 	; AB2
{0x0041,0x0a},  // 	; Image Size Manual
{0x0042,0x05},  // 	; 1280
{0x0043,0x00},  // 
{0x0044,0x03},  // 	; 960
{0x0045,0xC0},  // 
{0x0046,0x02},  // 	; DataFormat_YCbCr_Custom
{0x0060,0x00},  //
{0x0061,0x00},  //
{0x0066,0x02},  //
{0x0083,0x00},  //        ; Horizontal Mirror Enable
{0x0084,0x00},  //        ; Vertical Flip Enable
{0x0085,0x03},  //	; YCbYCr Order
{0x00B2,0x50},  //        ; set PLL output 713MHz
{0x00B3,0x80},  //
{0x00B4,0x01},  //        ; E_div
{0x00B5,0x01},  //        ; PLL3_div
{0x00E8,0x01},  //
{0x00ED,0x10},  //	; Min Framerate
{0x00EE,0x1E},  //	; Max Framerate
{0x0129,0x00},  //
{0x0130,0x00},  //
{0x019C,0x4B},  //
{0x019D,0xC0},  //
{0x01A0,0x01},  //
{0x01A1,0x80},  //
{0x01A2,0x80},  //
{0x01A3,0x80},  //
{0x5200,0x01},  //
{0x7000,0x0C},  //
{0x7101,0xC4},  //
{0x7102,0x09},  //
{0x7103,0x00},  //
{0x7104,0x00},  //	; OIF threshold = 128
{0x7105,0x80},  //	;
{0x7158,0x00},  //
{0x0143,0x5F},  //
{0x0144,0x0D},  //
{0x02C2,0x00},  //
{0x02C3,0xC0},  //
{0x015E,0x40},  //
{0x015F,0x00},  //
{0x0390,0x01},  //	; ArcticControl fArcticEnable
{0x0391,0x00},  //	; ArcticControl fArcticConfig DEFAULT CONFIG
{0x0392,0x00},  //	; ArcticControl fGNFConfig    DEFAULT CONFIG
{0x03A0,0x14},  //	; ArcticCCSigmaControl fMaximumCCSigma 
{0x03A1,0x00},  //	; ArcticCCSigmaControl fDisablePromotion {CompiledExposureTime}
{0x03A2,0x5A},  //	; ArcticCCSigmaControl fDamperLowThreshold {MSB}   //2400
{0x03A3,0xEE},  //	; ArcticCCSigmaControl fDamperLowThreshold {LSB}   
{0x03A4,0x69},  //	; ArcticCCSigmaControl fDamperHighThreshold {MSB}   //3444736
{0x03A5,0x49},  //	; ArcticCCSigmaControl fDamperHighThreshold {LSB}
{0x03A6,0x3E},  //	; ArcticCCSigmaControl fY1 {MSB}  // Low threshold
{0x03A7,0x00},  //	; ArcticCCSigmaControl fY1 {LSB} 
{0x03A8,0x39},  //	; ArcticCCSigmaControl fY2 {MSB}  // High threshold
{0x03A9,0x33},  //	; ArcticCCSigmaControl fY2 {LSB} 
{0x03B0,0x60},  //	; ArcticCCSigmaControl fMaximumRing 
{0x03B1,0x00},  //	; ArcticCCSigmaControl fDisablePromotion {CompiledExposureTime}
{0x03B2,0x5A},  //	; ArcticCCSigmaControl fDamperLowThreshold {MSB}    //24000
{0x03B3,0xEE},  //	; ArcticCCSigmaControl fDamperLowThreshold {LSB}
{0x03B4,0x69},  //	; ArcticCCSigmaControl DamperHighThreshold {MSB}    //3444736
{0x03B5,0x49},  //	; ArcticCCSigmaControl DamperHighThreshold {LSB}
{0x03B6,0x3E},  //	; ArcticCCSigmaControl fY1 {MSB}  //Low threshold
{0x03B7,0x00},  //	; ArcticCCSigmaControl fY1 {LSB} 
{0x03B8,0x3D},  //	; ArcticCCSigmaControl fY2 {MSB}  //High threshold
{0x03B9,0x20},  //	; ArcticCCSigmaControl fY2 {LSB}  
{0x03C0,0x10},  //	; ArcticCCSigmaControl fMaximumScoring 
{0x03C1,0x00},  //	; ArcticCCSigmaControl fDisablePromotion {CompiledExposureTime}
{0x03C2,0x5A},  //	; ArcticCCSigmaControl fDamperLowThreshold {MSB}    //24000
{0x03C3,0xEE},  //	; ArcticCCSigmaControl fDamperLowThreshold {LSB}
{0x03C4,0x69},  //	; ArcticCCSigmaControl DamperHighThreshold {MSB}    //3444736
{0x03C5,0x49},  //	; ArcticCCSigmaControl DamperHighThreshold {LSB}
{0x03C6,0x3A},  //	; ArcticCCSigmaControl fMinimumDamperOutput {MSB}
{0x03C7,0x80},  //	; ArcticCCSigmaControl fMinimumDamperOutput {LSB} 
{0x03D0,0x64},  //	; ArcticCCSigmaControl fMaximumScoring 
{0x03D1,0x00},  //	; ArcticCCSigmaControl fDisablePromotion {CompiledExposureTime}
{0x03D2,0x5A},  //	; ArcticCCSigmaControl fDamperLowThreshold {MSB}   //24000
{0x03D3,0xEE},  //	; ArcticCCSigmaControl fDamperLowThreshold {LSB}
{0x03D4,0x69},  //	; ArcticCCSigmaControl DamperHighThreshold {MSB}   //3444736
{0x03D5,0x49},  //	; ArcticCCSigmaControl DamperHighThreshold {LSB}
{0x03D6,0x34},  //	; ArcticCCSigmaControl fMinimumDamperOutput {MSB}
{0x03D7,0xD1},  //	; ArcticCCSigmaControl fMinimumDamperOutput {LSB} 
{0x004C,0x08},  //	; PipeSetupBank0 fPeakingGain
{0x006C,0x08},  //	; PipeSetupBank1 fPeakingGain
{0x0350,0x00},  //	; PeakingControl fDisableGainDamping  {CompiledExposureTime}
{0x0351,0x5A},  //	; PeakingControl fDamperLowThreshold_Gain  {MSB}   //24000
{0x0352,0xEE},  //	; PeakingControl fDamperLowThreshold_Gain  {LSB}
{0x0353,0x69},  //	; PeakingControl fDamperHighThreshold_Gain  {MSB}  //3444736
{0x0354,0x49},  //	; PeakingControl fDamperHighThreshold_Gain  {LSB}
{0x0355,0x39},  //	; PeakingControl fMinimumDamperOutput_Gain  {MSB}
{0x0356,0x6D},  //	; PeakingControl fMinimumDamperOutput_Gain  {LSB}
{0x0357,0x19},  //	; PeakingControl fUserPeakLoThresh 
{0x0358,0x00},  //	; PeakingControl fDisableCoringDamping  {CompiledExposureTime}
{0x0359,0x3C},  //	; PeakingControl fUserPeakHiThresh
{0x035A,0x5A},  //	; PeakingControl fDamperLowThreshold_Coring  {MSB}  //24000
{0x035B,0xEE},  //	; PeakingControl fDamperLowThreshold_Coring  {LSB}
{0x035C,0x69},  //	; PeakingControl fDamperHighThreshold_Coring {MSB}  //3444736
{0x035D,0x49},  //	; PeakingControl fDamperHighThreshold_Coring  {LSB}
{0x035E,0x39},  //	; PeakingControl fMinimumDamperOutput_Coring  {MSB}
{0x035F,0x85},  //	; PeakingControl fMinimumDamperOutput_Coring  {LSB}
{0x0049,0x14},  //	; PipeSetupBank0 bGammaGain 
{0x004A,0x0D},  //	; PipeSetupBank0 bGammaInterpolationGain
{0x0069,0x14},  //	; PipeSetupBank1 bGammaGain 
{0x006A,0x0D},  //	; PipeSetupBank1 bGammaInterpolationGain
{0x0090,0x5A},  //	; GammaGainDamperControl fpX1 {MSB}   //24000
{0x0091,0xEE},  //	; GammaGainDamperControl fpX1 {LSB}
{0x0092,0x3E},  //	; GammaGainDamperControl fpY1 {MSB}   //1
{0x0093,0x00},  //	; GammaGainDamperControl fpY1 {LSB}
{0x0094,0x69},  //	; GammaGainDamperControl fpX2 {MSB}   //3444736
{0x0095,0x49},  //	; GammaGainDamperControl fpX2 {LSB}
{0x0096,0x39},  //	; GammaGainDamperControl fpY2 {MSB}   //0.238
{0x0097,0xCF},  //	; GammaGainDamperControl fpY2 {LSB}
{0x0098,0x00},  //  ; GammaGainDamperControl fDisable {CompiledExposureTime}
{0x00A0,0x5A},  //	; GammaInterpolationDamperControl fpX1 {MSB}   //24000 
{0x00A1,0xEE},  //	; GammaInterpolationDamperControl fpX1 {LSB} 
{0x00A2,0x3E},  //	; GammaInterpolationDamperControl fpY1 {MSB}   //1 
{0x00A3,0x00},  //	; GammaInterpolationDamperControl fpY1 {LSB} 
{0x00A4,0x69},  //	; GammaInterpolationDamperControl fpX2 {MSB}   //3444736 
{0x00A5,0x49},  //	; GammaInterpolationDamperControl fpX2 {LSB} 
{0x00A6,0x3B},  //	; GammaInterpolationDamperControl fpY2 {MSB}   //0.4375 
{0x00A7,0x80},  //	; GammaInterpolationDamperControl fpY2 {LSB} 
{0x00A8,0x00},  //	; GammaInterpolationDamperControl fDisable {CompiledExposureTime} 
{0x0420,0x00},  //    ; C0_GreenRed_X 145
{0x0421,0x91},  //    ; C0_GreenRed_X LSB
{0x0422,0xff},  //    ; C0_GreenRed_Y -108
{0x0423,0x94},  //    ; C0_GreenRed_Y LSB
{0x0424,0x00},  //    ; C0_GreenRed_X2 125
{0x0425,0x7d},  //    ; C0_GreenRed_X2 LSB
{0x0426,0x00},  //    ; C0_GreenRed_Y2 170
{0x0427,0xaa},  //    ; C0_GreenRed_Y2 LSB
{0x0428,0x00},  //    ; C0_GreenRed_XY 57
{0x0429,0x39},  //    ; C0_GreenRed_XY LSB
{0x042a,0xff},  //    ; C0_GreenRed_X2Y -75
{0x042b,0xb5},  //    ; C0_GreenRed_X2Y LSB
{0x042c,0x01},  //    ; C0_GreenRed_XY2 444
{0x042d,0xbc},  //    ; C0_GreenRed_XY2 LSB
{0x042e,0xff},  //    ; C0_GreenRed_X2Y2 -45
{0x042f,0xd3},  //    ; C0_GreenRed_X2Y2 LSB
{0x0430,0x00},  //    ; C0_Red_X 175
{0x0431,0xaf},  //    ; C0_Red_X LSB
{0x0432,0xff},  //    ; C0_Red_Y -164
{0x0433,0x5c},  //    ; C0_Red_Y LSB
{0x0434,0x00},  //    ; C0_Red_X2 248
{0x0435,0xf8},  //    ; C0_Red_X2 LSB
{0x0436,0x01},  //    ; C0_Red_Y2 285
{0x0437,0x1d},  //    ; C0_Red_Y2 LSB
{0x0438,0xff},  //    ; C0_Red_XY -127
{0x0439,0x81},  //    ; C0_Red_XY LSB
{0x043a,0xff},  //    ; C0_Red_X2Y -143
{0x043b,0x71},  //    ; C0_Red_X2Y LSB
{0x043c,0x01},  //    ; C0_Red_XY2 375
{0x043d,0x77},  //    ; C0_Red_XY2 LSB
{0x043e,0xff},  //    ; C0_Red_X2Y2 -174
{0x043f,0x52},  //    ; C0_Red_X2Y2 LSB
{0x0450,0x00},  //    ; C0_Blue_X 117
{0x0451,0x75},  //    ; C0_Blue_X LSB
{0x0452,0xff},  //    ; C0_Blue_Y -114
{0x0453,0x8e},  //    ; C0_Blue_Y LSB
{0x0454,0x00},  //    ; C0_Blue_X2 106
{0x0455,0x6a},  //    ; C0_Blue_X2 LSB
{0x0456,0x00},  //    ; C0_Blue_Y2 138
{0x0457,0x8a},  //    ; C0_Blue_Y2 LSB
{0x0458,0xff},  //    ; C0_Blue_XY -90
{0x0459,0xa6},  //    ; C0_Blue_XY LSB
{0x045a,0xff},  //    ; C0_Blue_X2Y -86
{0x045b,0xaa},  //    ; C0_Blue_X2Y LSB
{0x045c,0x00},  //    ; C0_Blue_XY2 224
{0x045d,0xe0},  //    ; C0_Blue_XY2 LSB
{0x045e,0xff},  //    ; C0_Blue_X2Y2 -48
{0x045f,0xd0},  //    ; C0_Blue_X2Y2 LSB
{0x0440,0x00},  //    ; C0_GreenBlue_X 91
{0x0441,0x5b},  //    ; C0_GreenBlue_X LSB
{0x0442,0xff},  //    ; C0_GreenBlue_Y -123
{0x0443,0x85},  //    ; C0_GreenBlue_Y LSB
{0x0444,0x00},  //    ; C0_GreenBlue_X2 145
{0x0445,0x91},  //    ; C0_GreenBlue_X2 LSB
{0x0446,0x00},  //    ; C0_GreenBlue_Y2 151
{0x0447,0x97},  //    ; C0_GreenBlue_Y2 LSB
{0x0448,0x00},  //    ; C0_GreenBlue_XY 44
{0x0449,0x2c},  //    ; C0_GreenBlue_XY LSB
{0x044a,0xff},  //    ; C0_GreenBlue_X2Y -207
{0x044b,0x31},  //    ; C0_GreenBlue_X2Y LSB
{0x044c,0x01},  //    ; C0_GreenBlue_XY2 349
{0x044d,0x5d},  //    ; C0_GreenBlue_XY2 LSB
{0x044e,0xff},  //    ; C0_GreenBlue_X2Y2 -42
{0x044f,0xd6},  //    ; C0_GreenBlue_X2Y2 LSB
{0x0460,0x00},  //    ; C1_GreenRed_X 148
{0x0461,0x94},  //    ; C1_GreenRed_X LSB
{0x0462,0xff},  //    ; C1_GreenRed_Y -106
{0x0463,0x96},  //    ; C1_GreenRed_Y LSB
{0x0464,0x00},  //    ; C1_GreenRed_X2 124
{0x0465,0x7c},  //    ; C1_GreenRed_X2 LSB
{0x0466,0x00},  //    ; C1_GreenRed_Y2 165
{0x0467,0xa5},  //    ; C1_GreenRed_Y2 LSB
{0x0468,0x00},  //    ; C1_GreenRed_XY 66
{0x0469,0x42},  //    ; C1_GreenRed_XY LSB
{0x046a,0xff},  //    ; C1_GreenRed_X2Y -80
{0x046b,0xb0},  //    ; C1_GreenRed_X2Y LSB
{0x046c,0x01},  //    ; C1_GreenRed_XY2 450
{0x046d,0xc2},  //    ; C1_GreenRed_XY2 LSB
{0x046e,0xff},  //    ; C1_GreenRed_X2Y2 -35
{0x046f,0xdd},  //    ; C1_GreenRed_X2Y2 LSB
{0x0470,0x00},  //    ; C1_Red_X 170
{0x0471,0xaa},  //    ; C1_Red_X LSB
{0x0472,0xff},  //    ; C1_Red_Y -155
{0x0473,0x65},  //    ; C1_Red_Y LSB
{0x0474,0x00},  //    ; C1_Red_X2 234
{0x0475,0xea},  //    ; C1_Red_X2 LSB
{0x0476,0x01},  //    ; C1_Red_Y2 270
{0x0477,0x0e},  //    ; C1_Red_Y2 LSB
{0x0478,0xff},  //    ; C1_Red_XY -121
{0x0479,0x87},  //    ; C1_Red_XY LSB
{0x047a,0xff},  //    ; C1_Red_X2Y -145
{0x047b,0x6f},  //    ; C1_Red_X2Y LSB
{0x047c,0x01},  //    ; C1_Red_XY2 360
{0x047d,0x68},  //    ; C1_Red_XY2 LSB
{0x047e,0xff},  //    ; C1_Red_X2Y2 -152
{0x047f,0x68},  //    ; C1_Red_X2Y2 LSB
{0x0490,0x00},  //    ; C1_Blue_X 125
{0x0491,0x7d},  //    ; C1_Blue_X LSB
{0x0492,0xff},  //    ; C1_Blue_Y -110
{0x0493,0x92},  //    ; C1_Blue_Y LSB
{0x0494,0x00},  //    ; C1_Blue_X2 103
{0x0495,0x67},  //    ; C1_Blue_X2 LSB
{0x0496,0x00},  //    ; C1_Blue_Y2 132
{0x0497,0x84},  //    ; C1_Blue_Y2 LSB
{0x0498,0xff},  //    ; C1_Blue_XY -93
{0x0499,0xa3},  //    ; C1_Blue_XY LSB
{0x049a,0xff},  //    ; C1_Blue_X2Y -104
{0x049b,0x98},  //    ; C1_Blue_X2Y LSB
{0x049c,0x01},  //    ; C1_Blue_XY2 256
{0x049d,0x00},  //    ; C1_Blue_XY2 LSB
{0x049e,0xff},  //    ; C1_Blue_X2Y2 -32
{0x049f,0xe0},  //    ; C1_Blue_X2Y2 LSB
{0x0480,0x00},  //    ; C1_GreenBlue_X 90
{0x0481,0x5a},  //    ; C1_GreenBlue_X LSB
{0x0482,0xff},  //    ; C1_GreenBlue_Y -116
{0x0483,0x8c},  //    ; C1_GreenBlue_Y LSB
{0x0484,0x00},  //    ; C1_GreenBlue_X2 141
{0x0485,0x8d},  //    ; C1_GreenBlue_X2 LSB
{0x0486,0x00},  //    ; C1_GreenBlue_Y2 148
{0x0487,0x94},  //    ; C1_GreenBlue_Y2 LSB
{0x0488,0x00},  //    ; C1_GreenBlue_XY 55
{0x0489,0x37},  //    ; C1_GreenBlue_XY LSB
{0x048a,0xff},  //    ; C1_GreenBlue_X2Y -226
{0x048b,0x1e},  //    ; C1_GreenBlue_X2Y LSB
{0x048c,0x01},  //    ; C1_GreenBlue_XY2 349
{0x048d,0x5d},  //    ; C1_GreenBlue_XY2 LSB
{0x048e,0xff},  //    ; C1_GreenBlue_X2Y2 -33
{0x048f,0xdf},  //    ; C1_GreenBlue_X2Y2 LSB
{0x04a0,0x00},  //    ; C2_GreenRed_X 150
{0x04a1,0x96},  //    ; C2_GreenRed_X LSB
{0x04a2,0xff},  //    ; C2_GreenRed_Y -97
{0x04a3,0x9f},  //    ; C2_GreenRed_Y LSB
{0x04a4,0x00},  //    ; C2_GreenRed_X2 117
{0x04a5,0x75},  //    ; C2_GreenRed_X2 LSB
{0x04a6,0x00},  //    ; C2_GreenRed_Y2 150
{0x04a7,0x96},  //    ; C2_GreenRed_Y2 LSB
{0x04a8,0x00},  //    ; C2_GreenRed_XY 62
{0x04a9,0x3e},  //    ; C2_GreenRed_XY LSB
{0x04aa,0xff},  //    ; C2_GreenRed_X2Y -118
{0x04ab,0x8a},  //    ; C2_GreenRed_X2Y LSB
{0x04ac,0x01},  //    ; C2_GreenRed_XY2 456
{0x04ad,0xc8},  //    ; C2_GreenRed_XY2 LSB
{0x04ae,0xff},  //    ; C2_GreenRed_X2Y2 -12
{0x04af,0xf4},  //    ; C2_GreenRed_X2Y2 LSB
{0x04b0,0x00},  //    ; C2_Red_X 136
{0x04b1,0x88},  //    ; C2_Red_X LSB
{0x04b2,0xff},  //    ; C2_Red_Y -118
{0x04b3,0x8a},  //    ; C2_Red_Y LSB
{0x04b4,0x00},  //    ; C2_Red_X2 166
{0x04b5,0xa6},  //    ; C2_Red_X2 LSB
{0x04b6,0x00},  //    ; C2_Red_Y2 194
{0x04b7,0xc2},  //    ; C2_Red_Y2 LSB
{0x04b8,0xff},  //    ; C2_Red_XY -96
{0x04b9,0xa0},  //    ; C2_Red_XY LSB
{0x04ba,0xff},  //    ; C2_Red_X2Y -229
{0x04bb,0x1b},  //    ; C2_Red_X2Y LSB
{0x04bc,0x01},  //    ; C2_Red_XY2 339
{0x04bd,0x53},  //    ; C2_Red_XY2 LSB
{0x04be,0xff},  //    ; C2_Red_X2Y2 -50
{0x04bf,0xce},  //    ; C2_Red_X2Y2 LSB
{0x04d0,0x00},  //    ; C2_Blue_X 137
{0x04d1,0x89},  //    ; C2_Blue_X LSB
{0x04d2,0xff},  //    ; C2_Blue_Y -101
{0x04d3,0x9b},  //    ; C2_Blue_Y LSB
{0x04d4,0x00},  //    ; C2_Blue_X2 94
{0x04d5,0x5e},  //    ; C2_Blue_X2 LSB
{0x04d6,0x00},  //    ; C2_Blue_Y2 120
{0x04d7,0x78},  //    ; C2_Blue_Y2 LSB
{0x04d8,0xff},  //    ; C2_Blue_XY -107
{0x04d9,0x95},  //    ; C2_Blue_XY LSB
{0x04da,0xff},  //    ; C2_Blue_X2Y -135
{0x04db,0x79},  //    ; C2_Blue_X2Y LSB
{0x04dc,0x01},  //    ; C2_Blue_XY2 274
{0x04dd,0x12},  //    ; C2_Blue_XY2 LSB
{0x04de,0xff},  //    ; C2_Blue_X2Y2 -4
{0x04df,0xfc},  //    ; C2_Blue_X2Y2 LSB
{0x04c0,0x00},  //    ; C2_GreenBlue_X 91
{0x04c1,0x5b},  //    ; C2_GreenBlue_X LSB
{0x04c2,0xff},  //    ; C2_GreenBlue_Y -100
{0x04c3,0x9c},  //    ; C2_GreenBlue_Y LSB
{0x04c4,0x00},  //    ; C2_GreenBlue_X2 130
{0x04c5,0x82},  //    ; C2_GreenBlue_X2 LSB
{0x04c6,0x00},  //    ; C2_GreenBlue_Y2 139
{0x04c7,0x8b},  //    ; C2_GreenBlue_Y2 LSB
{0x04c8,0x00},  //    ; C2_GreenBlue_XY 54
{0x04c9,0x36},  //    ; C2_GreenBlue_XY LSB
{0x04ca,0xfe},  //    ; C2_GreenBlue_X2Y -260
{0x04cb,0xfc},  //    ; C2_GreenBlue_X2Y LSB
{0x04cc,0x01},  //    ; C2_GreenBlue_XY2 380
{0x04cd,0x7c},  //    ; C2_GreenBlue_XY2 LSB
{0x04ce,0xff},  //    ; C2_GreenBlue_X2Y2 -10
{0x04cf,0xf6},  //    ; C2_GreenBlue_X2Y2 LSB
{0x04e0,0x00},  //    ; C3_GreenRed_X 149
{0x04e1,0xea},  //    ; C3_GreenRed_X LSB
{0x04e2,0xff},  //    ; C3_GreenRed_Y -90
{0x04e3,0xa6},  //    ; C3_GreenRed_Y LSB
{0x04e4,0x00},  //    ; C3_GreenRed_X2 118
{0x04e5,0x76},  //    ; C3_GreenRed_X2 LSB
{0x04e6,0x00},  //    ; C3_GreenRed_Y2 148
{0x04e7,0x94},  //    ; C3_GreenRed_Y2 LSB
{0x04e8,0xff},  //    ; C3_GreenRed_XY 76
{0x04e9,0x8b},  //    ; C3_GreenRed_XY LSB
{0x04ea,0xff},  //    ; C3_GreenRed_X2Y -64
{0x04eb,0xc0},  //    ; C3_GreenRed_X2Y LSB
{0x04ec,0x01},  //    ; C3_GreenRed_XY2 449
{0x04ed,0xc1},  //    ; C3_GreenRed_XY2 LSB
{0x04ee,0xff},  //    ; C3_GreenRed_X2Y2 -24
{0x04ef,0xe8},  //    ; C3_GreenRed_X2Y2 LSB
{0x04f0,0x00},  //    ; C3_Red_X 146
{0x04f1,0xfb},  //    ; C3_Red_X LSB
{0x04f2,0xff},  //    ; C3_Red_Y -119
{0x04f3,0x89},  //    ; C3_Red_Y LSB
{0x04f4,0x00},  //    ; C3_Red_X2 179
{0x04f5,0xd0},  //    ; C3_Red_X2 LSB
{0x04f6,0x00},  //    ; C3_Red_Y2 215
{0x04f7,0xd0},  //    ; C3_Red_Y2 LSB
{0x04f8,0x01},  //    ; C3_Red_XY -84
{0x04f9,0x3e},  //    ; C3_Red_XY LSB
{0x04fa,0xff},  //    ; C3_Red_X2Y -122
{0x04fb,0x86},  //    ; C3_Red_X2Y LSB
{0x04fc,0x01},  //    ; C3_Red_XY2 287
{0x04fd,0x1f},  //    ; C3_Red_XY2 LSB
{0x04fe,0xff},  //    ; C3_Red_X2Y2 -102
{0x04ff,0x20},  //    ; C3_Red_X2Y2 LSB
{0x0510,0x00},  //    ; C3_Blue_X 144
{0x0511,0xff},  //    ; C3_Blue_X LSB
{0x0512,0xff},  //    ; C3_Blue_Y -94
{0x0513,0xa2},  //    ; C3_Blue_Y LSB
{0x0514,0x00},  //    ; C3_Blue_X2 96
{0x0515,0x60},  //    ; C3_Blue_X2 LSB
{0x0516,0x00},  //    ; C3_Blue_Y2 123
{0x0517,0x7b},  //    ; C3_Blue_Y2 LSB
{0x0518,0xff},  //    ; C3_Blue_XY -118
{0x0519,0x8a},  //    ; C3_Blue_XY LSB
{0x051a,0xff},  //    ; C3_Blue_X2Y -126
{0x051b,0x82},  //    ; C3_Blue_X2Y LSB
{0x051c,0xff},  //    ; C3_Blue_XY2 272
{0x051d,0x8a},  //    ; C3_Blue_XY2 LSB
{0x051e,0x00},  //    ; C3_Blue_X2Y2 -19
{0x051f,0x10},  //    ; C3_Blue_X2Y2 LSB
{0x0500,0x00},  //    ; C3_GreenBlue_X 84
{0x0501,0xea},  //    ; C3_GreenBlue_X LSB
{0x0502,0xff},  //    ; C3_GreenBlue_Y -99
{0x0503,0x9d},  //    ; C3_GreenBlue_Y LSB
{0x0504,0x00},  //    ; C3_GreenBlue_X2 125
{0x0505,0x7d},  //    ; C3_GreenBlue_X2 LSB
{0x0506,0x00},  //    ; C3_GreenBlue_Y2 142
{0x0507,0x8e},  //    ; C3_GreenBlue_Y2 LSB
{0x0508,0xff},  //    ; C3_GreenBlue_XY 75
{0x0509,0x8b},  //    ; C3_GreenBlue_XY LSB
{0x050a,0xff},  //    ; C3_GreenBlue_X2Y -200
{0x050b,0x38},  //    ; C3_GreenBlue_X2Y LSB
{0x050c,0x01},  //    ; C3_GreenBlue_XY2 348
{0x050d,0x5c},  //    ; C3_GreenBlue_XY2 LSB
{0x050e,0xff},  //    ; C3_GreenBlue_X2Y2 -21
{0x050f,0xeb},  //    ; C3_GreenBlue_X2Y2 LSB
{0x0561,0x0d},  //    ;C0 Unity
{0x0562,0x0a},  //    ;C1 Unity
{0x0563,0x06},  //    ;C2 Unity
{0x0564,0x01},  //    ;C3 Unity
{0x0324,0x39},  //    ; NormRedGain_Cast0 Hor
{0x0325,0xAE},  //    ; NormRedGain_Cast0_LSB 
{0x0326,0x3A},  //    ; NormRedGain_Cast1 IncA
{0x0327,0x29},  //    ; NormRedGain_Cast1_LSB 
{0x0328,0x3B},  //    ; NormRedGain_Cast2 CWF
{0x0329,0x0A},  //    ; NormRedGain_Cast2_LSB 
{0x032A,0x3B},  //    ; NormRedGain_Cast3 D65
{0x032B,0x62},  //    ; NormRedGain_Cast3_LSB 
{0x0320,0x01},  //    ; AntiVignetteControl - Enable
{0x0321,0x04},  //    ; NbOfPresets
{0x0322,0x01},  //    ; AdaptiveAntiVignetteControlEnable - Enable
{0x0323,0x01},  //    ; LoLightAntiVignetteControlDisable - Damper Off
{0x0330,0x01},  //	; Turn off colour matrix damper
{0x0384,0x00},  //	; Turn off colour effects
{0x0337,0x01},  //	; Turn on adaptive colour matrix
{0x03EC,0x39},  //	; Matrix 0
{0x03ED,0x85},  //	; LSB
{0x03FC,0x3A},  //	; Matrix 1
{0x03FD,0x14},  //	; LSB
{0x040C,0x3A},  //	; Matrix 2
{0x040D,0xF6},  //	; LSB
{0x041C,0x3B},  //	; Matrix 3
{0x041D,0x9A},  //	; LSB
{0x03E0,0xB6},  //	; GInR
{0x03E1,0x04},  //	;
{0x03E2,0xBB},  //	; BInR
{0x03E3,0xE9},  //	;
{0x03E4,0xBC},  //	; RInG
{0x03E5,0x70},  //	;
{0x03E6,0x37},  //	; BInG
{0x03E7,0x02},  //	;
{0x03E8,0xBC},  //	; RInB
{0x03E9,0x00},  //	;
{0x03EA,0xBF},  //	; GInB
{0x03EB,0x12},  //	;
{0x03F0,0xBA},  //	; GInR
{0x03F1,0x7B},  //	;
{0x03F2,0xBA},  //	; BInR
{0x03F3,0x83},  //	;
{0x03F4,0xBB},  //	; RInG
{0x03F5,0xBC},  //	;
{0x03F6,0x38},  //	; BInG
{0x03F7,0x2D},  //	;
{0x03F8,0xBB},  //	; RInB
{0x03F9,0x23},  //	;
{0x03FA,0xBD},  //	; GInB
{0x03FB,0xAC},  //	;
{0x0400,0xBE},  //	; GInR
{0x0401,0x96},  //	;
{0x0402,0xB9},  //	; BInR
{0x0403,0xBE},  //	;
{0x0404,0xBB},  //	; RInG
{0x0405,0x57},  //	;
{0x0406,0x3A},  //	; BInG
{0x0407,0xBB},  //	;
{0x0408,0xB3},  //	; RInB
{0x0409,0x17},  //	;
{0x040A,0xBE},  //	; GInB
{0x040B,0x66},  //	;
{0x0410,0xBB},  //	; GInR
{0x0411,0x2A},  //	;
{0x0412,0xBA},  //	; BInR
{0x0413,0x00},  //	;
{0x0414,0xBB},  //	; RInG
{0x0415,0x10},  //	;
{0x0416,0xB8},  //	; BInG
{0x0417,0xCD},  //	;
{0x0418,0xB7},  //	; RInB
{0x0419,0x5C},  //	;
{0x041A,0xBB},  //	; GInB
{0x041B,0x6C},  //	;
{0x01f8,0x3c},  //    ;fpMaximumDistanceAllowedFromLocus
{0x01f9,0x00},  //    ;=0.5
{0x01fa,0x00},  //    ;fEnableConstrainedWhiteBalance = false
{0x02a2,0x3e},  //    ;fpRedTilt
{0x02a3,0x00},  //    ;= 1.00
{0x02a4,0x3e},  //    ;fpGreenTilt1
{0x02a5,0x00},  //    ;= 1.00
{0x02a6,0x3e},  //    ;fpGreenTilt2
{0x02a7,0x00},  //    ;= 1.00
{0x02a8,0x3e},  //    ;fpBlueTilt
{0x02a9,0x00},  //    ;= 1.00
{0x056c,0x42},  //    ;fpRedTilt
{0x056d,0x00},  //    ;= 4.00
{0x056e,0x42},  //    ;fpGreenTilt1
{0x056f,0x00},  //    ;= 4.00
{0x0570,0x42},  //    ;fpGreenTilt2
{0x0571,0x00},  //    ;= 4.00
{0x0572,0x42},  //    ;fpBlueTilt
{0x0573,0x00},  //    ;= 4.00
{0x0081,0x58},  //	; PipeSetupCommon bColourSaturation
{0x0588,0x00},  //	; ColourSaturationDamper fDisable {CompiledExposureTime}
{0x0589,0x5A},  //	; ColourSaturationDamper fpLowThreshold {MSB}
{0x058A,0xEE},  //	; ColourSaturationDamper fpLowThreshold {LSB}
{0x058B,0x69},  //	; ColourSaturationDamper fpHighThreshold {MSB}
{0x058C,0x49},  //	; ColourSaturationDamper fpHighThreshold {LSB}
{0x058D,0x3D},  //	; ColourSaturationDamper fpMinimumOutput {MSB}
{0x058E,0x3D},  //	; ColourSaturationDamper fpMinimumOutput {LSB}
{0x0080,0x6C},  //	; PipeSetupCommon bContrast
{0x0082,0x5A},  //	; PipeSetupCommon bBrightness
{0x065A,0x00},  //    ; AFStatsControls->bWindowsSystem = 7 zone AF system 
{0x06C9,0x01},  //    ; FLADriverLowLevelParameters->AutoSkipNextFrame = ENABLED
{0x06CD,0x01},  //    ; FLADriverLowLevelParameters->AF_OTP_uwHostDefMacro MSB = 445
{0x06CE,0xBD},  //    ; FLADriverLowLevelParameters->AF_OTP_uwHostDefMacro LSB
{0x06CF,0x00},  //    ; FLADriverLowLevelParameters->AF_OTP_uwHostDefInfinity MSB = 147
{0x06D0,0x93},  //    ; FLADriverLowLevelParameters->AF_OTP_uwHostDefInfinity LSB
{0x06D1,0x02},  //    ; FLADriverLowLevelParameters->AF_OTP_bStepsMultiStepDriver = 2 step driver
{0x06D2,0x30},  //    ; FLADriverLowLevelParameters->AF_OTP_uwMultiStepTimeDelay MSB = 12.5ms
{0x06D3,0xD4},  //    ; FLADriverLowLevelParameters->AF_OTP_uwMultiStepTimeDelay LSB
{0x06D4,0x01},  //    ; FLADriverLowLevelParameters->AF_OTP_fHostEnableOTPRead (1 = disabled)
{0x06DB,0x59},  //    ; FLADriverLowLevelParameters->fpActuatorResponseTime MSB 12.5ms (FP900) 
{0x06DC,0x0d},  //    ; FLADriverLowLevelParameters->fpActuatorResponseTime LSB
{0x0730,0x00},  //    ; FocusRangeConstants->wFullRange_LensMinPosition MSB = 0
{0x0731,0x00},  //    ; FocusRangeConstants->wFullRange_LensMinPosition LSB
{0x0732,0x03},  //    ; FocusRangeConstants->wFullRange_LensMaxPosition MSB = 1023
{0x0733,0xFF},  //    ; FocusRangeConstants->wFullRange_LensMaxPosition LSB
{0x0734,0x03},  //    ; FocusRangeConstants->wFullRange_LensRecoveryPosition MSB = 880
{0x0735,0x70},  //    ; FocusRangeConstants->wFullRange_LensRecoveryPosition LSB
{0x0755,0x01},  //    ; AutoFocusControls->fEnableSimpleCoarseThEvaluation = ENABLED
{0x0756,0x03},  //    ; AutoFocusControls->bSelectedMultizoneBehavior = REGIONSELECTIONMETHOD_AVERAGE //mk change to 0x3
{0x075B,0x01},  //    ; AutoFocusControls->fEnableTrackingThresholdEvaluation = DISABLED //MK change to 0x1
{0x075E,0x00},  //    ; AutoFocusControls->fFineToCoarseAutoTransitionEnable = DISABLED
{0x0764,0x01},  //    ; AutoFocusControls->fResetHCSPos = TRUE = Start from Recovery Position for every HCS
{0x0766,0x01},  //    ; AutoFocusControls->fEnablePrioritiesMacro = FALSE = Do not prioritise Macro //mk change to 0x01
{0x0768,0x01},  //    ; AutoFocusControls->fEnableInterpolationAfterFineSearch = TRUE
{0x076A,0x00},  //    ; AutoFocusControls->fReducedZoneSetup = TRUE //mk change to 0x0
{0x0758,0x01},  //    ; AutoFocusControls->bWeighedFunctionSelected = TRAPEZIUM
{0x075C,0x01},  //    ; AutoFocusControls->fEnableHeuristicMethod = FALSE
{0x0770,0x98},  //    ; AutoFocusConstants->bCoarseStep = 95
{0x0771,0x19},  //    ; AutoFocusConstants->bFineStep = 16
{0x0772,0x1B},  //    ; AutoFocusConstants->bFullSearchStep = 27
{0x0774,0x01},  //    ; AutoFocusConstants->uwFineThreshold MSB = 330 
{0x0775,0x4a},  //    ; AutoFocusConstants->uwFineThreshold LSB 
{0x0777,0x00},  //    ; AutoFocusConstants->uwBacklightThreshold MSB = 69
{0x0778,0x45},  //    ; AutoFocusConstants->uwBacklightThreshold LSB 
{0x0779,0x00},  //    ; AutoFocusConstants->uwMotionBlurInRatio MSB = 2
{0x077A,0x02},  //    ; AutoFocusConstants->uwMotionBlurInRatio LSB
{0x077D,0x01},  //    ; AutoFocusConstants->bMaxNumberContinuouslyInstableTime = 1
{0x077E,0x03},  //    ; AutoFocusConstants->bMaxNumberContinuouslyStableFrame = 3
{0x0783,0x10},  //    ; AutoFocusConstants->bLightGap = 10 
{0x0785,0x14},  //    ; AutoFocusConstants->uwDeltaValue = 20
{0x0788,0x04},  //    ; AutoFocusConstants->bMinNumberMacroRegion = 4 //mk add
{0x0846,0x06},  //    ; AutoFocusHeuristicConstants->bHighToMaxFMShiftFactor = 6
{0x0847,0x05},  //    ; AutoFocusHeuristicConstants->bLowToHighFMShiftFactor = 5
{0xC41A,0x05},  //    ; TEST_LP_TX (clock slew rate)
{0xC423,0x11},  //    ; TEST_LP_TX_SLEW_RATE_DL1
{0xC427,0x11},  //    ; TEST_LP_TX_SLEW_RATE_DL2
{0x300B,0x09},  //    ; esc_clk_div (clk_sys div by 10)
{0x00B2,0x50},//4f
{0x00B3,0x80},//c0
{0x00B5,0x02},
{0x0030,0x14},
{0x0040,0x01},
{0x0041,0x04},
{0x0046,0x00},
{0x00EE,0x1E},
{0x0010,0x01},  //    ; CMD_RUN
{0x0714,0x00},
{0x4708,0x00},  //	; av2x2_h_offset
{0x4709,0x00},  //	; LSB
{0x4710,0x00},  //	; av2x2_v_offset {0x4710 & 11} are correct!
{0x4711,0x00},  //	; LSB
{0xffff,0xff},
};
#endif