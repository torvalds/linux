#ifndef COMMON_INCD
#define COMMON_INCD

typedef void VOID;
typedef u8 BOOLEAN;
typedef u8 BYTE;
typedef u8 *PBYTE;
typedef u16 WORD;
typedef u16 *PWORD;
typedef u32 DWORD;
typedef u32 *PDWORD;

#define swapWORD(w)	((((unsigned short)(w) << 8) & 0xff00) |	\
			 (((unsigned short)(w) >> 8) & 0x00ff))
#define swapDWORD(dw)	((((unsigned long)(dw) << 24) & 0xff000000) |	\
			 (((unsigned long)(dw) <<  8) & 0x00ff0000) |	\
			 (((unsigned long)(dw) >>  8) & 0x0000ff00) |	\
			 (((unsigned long)(dw) >> 24) & 0x000000ff))

#define LittleEndianWORD(w)	(w)
#define LittleEndianDWORD(dw)	(dw)
#define BigEndianWORD(w)	swapWORD(w)
#define BigEndianDWORD(dw)	swapDWORD(dw)

#endif

