
#define CSTART       0x400L
#define CMAX         0x800L
#define ISTART       0x800L
#define IMAX         0xC00L
#define CIN          0xD10L
#define GLOBAL       0xD10L
#define EIN          0xD18L
#define FEPSTAT      0xD20L
#define CHANSTRUCT   0x1000L
#define RXTXBUF      0x4000L


struct global_data 
{
	u16 cin;
	u16 cout;
	u16 cstart;
	u16 cmax;
	u16 ein;
	u16 eout;
	u16 istart;
	u16 imax;
};


struct board_chan 
{
	u32 filler1;
	u32 filler2;
	u16 tseg;
	u16 tin;
	u16 tout;
	u16 tmax;

	u16 rseg;
	u16 rin;
	u16 rout;
	u16 rmax;

	u16 tlow;
	u16 rlow;
	u16 rhigh;
	u16 incr;

	u16 etime;
	u16 edelay;
	unchar *dev;

	u16 iflag;
	u16 oflag;
	u16 cflag;
	u16 gmask;

	u16 col;
	u16 delay;
	u16 imask;
	u16 tflush;

	u32 filler3;
	u32 filler4;
	u32 filler5;
	u32 filler6;

	u8 num;
	u8 ract;
	u8 bstat;
	u8 tbusy;
	u8 iempty;
	u8 ilow;
	u8 idata;
	u8 eflag;

	u8 tflag;
	u8 rflag;
	u8 xmask;
	u8 xval;
	u8 mstat;
	u8 mchange;
	u8 mint;
	u8 lstat;

	u8 mtran;
	u8 orun;
	u8 startca;
	u8 stopca;
	u8 startc;
	u8 stopc;
	u8 vnext;
	u8 hflow;

	u8 fillc;
	u8 ochar;
	u8 omask;

	u8 filler7;
	u8 filler8[28];
}; 


#define SRXLWATER      0xE0
#define SRXHWATER      0xE1
#define STOUT          0xE2
#define PAUSETX        0xE3
#define RESUMETX       0xE4
#define SAUXONOFFC     0xE6
#define SENDBREAK      0xE8
#define SETMODEM       0xE9
#define SETIFLAGS      0xEA
#define SONOFFC        0xEB
#define STXLWATER      0xEC
#define PAUSERX        0xEE
#define RESUMERX       0xEF
#define SETBUFFER      0xF2
#define SETCOOKED      0xF3
#define SETHFLOW       0xF4
#define SETCTRLFLAGS   0xF5
#define SETVNEXT       0xF6



#define BREAK_IND        0x01
#define LOWTX_IND        0x02
#define EMPTYTX_IND      0x04
#define DATA_IND         0x08
#define MODEMCHG_IND     0x20

#define FEP_HUPCL  0002000
#if 0
#define RTS   0x02
#define CD    0x08
#define DSR   0x10
#define CTS   0x20
#define RI    0x40
#define DTR   0x80
#endif
