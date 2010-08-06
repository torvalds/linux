#ifndef _OSDEF_H_
#define _OSDEF_H_

/* #define WINCE_HEADER*/
/*#define WIN2000*/
/* #define TC */
#define LINUX_KERNEL
/* #define LINUX_XF86 */

/**********************************************************************/
#ifdef LINUX_KERNEL
//#include <linux/config.h>
#endif


/**********************************************************************/
#ifdef TC
#endif
#ifdef WIN2000
#endif
#ifdef WINCE_HEADER
#endif
#ifdef LINUX_XF86
#define LINUX
#endif
#ifdef LINUX_KERNEL
#define LINUX
#endif

/**********************************************************************/
#ifdef TC
#define XGI_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize);
#endif
#ifdef WIN2000
#define XGI_SetMemory(MemoryAddress,MemorySize,value) MemFill((PVOID) MemoryAddress,(ULONG) MemorySize,(UCHAR) value);
#endif
#ifdef WINCE_HEADER
#define XGI_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize);
#endif
#ifdef LINUX_XF86
#define XGI_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#endif
#ifdef LINUX_KERNEL
#define XGI_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#endif
/**********************************************************************/

/**********************************************************************/

#ifdef TC
#define XGI_MemoryCopy(Destination,Soruce,Length) memmove(Destination, Soruce, Length);
#endif
#ifdef WIN2000
#define XGI_MemoryCopy(Destination,Soruce,Length)  /*VideoPortMoveMemory((PUCHAR)Destination , Soruce,length);*/
#endif
#ifdef WINCE_HEADER
#define XGI_MemoryCopy(Destination,Soruce,Length) memmove(Destination, Soruce, Length);
#endif
#ifdef LINUX_XF86
#define XGI_MemoryCopy(Destination,Soruce,Length) memcpy(Destination,Soruce,Length)
#endif
#ifdef LINUX_KERNEL
#define XGI_MemoryCopy(Destination,Soruce,Length) memcpy(Destination,Soruce,Length)
#endif

/**********************************************************************/

#ifdef OutPortByte
#undef OutPortByte
#endif /* OutPortByte */

#ifdef OutPortWord
#undef OutPortWord
#endif /* OutPortWord */

#ifdef OutPortLong
#undef OutPortLong
#endif /* OutPortLong */

#ifdef InPortByte
#undef InPortByte
#endif /* InPortByte */

#ifdef InPortWord
#undef InPortWord
#endif /* InPortWord */

#ifdef InPortLong
#undef InPortLong
#endif /* InPortLong */

/**********************************************************************/
/*  TC                                                                */
/**********************************************************************/

#ifdef TC
#define OutPortByte(p,v) outp((unsigned short)(p),(unsigned char)(v))
#define OutPortWord(p,v) outp((unsigned short)(p),(unsigned short)(v))
#define OutPortLong(p,v) outp((unsigned short)(p),(unsigned long)(v))
#define InPortByte(p)    inp((unsigned short)(p))
#define InPortWord(p)    inp((unsigned short)(p))
#define InPortLong(p)    ((inp((unsigned short)(p+2))<<16) | inp((unsigned short)(p)))
#endif

/**********************************************************************/
/*  LINUX XF86                                                        */
/**********************************************************************/

#ifdef LINUX_XF86
#define OutPortByte(p,v) outb((CARD16)(p),(CARD8)(v))
#define OutPortWord(p,v) outw((CARD16)(p),(CARD16)(v))
#define OutPortLong(p,v) outl((CARD16)(p),(CARD32)(v))
#define InPortByte(p)    inb((CARD16)(p))
#define InPortWord(p)    inw((CARD16)(p))
#define InPortLong(p)    inl((CARD16)(p))
#endif

#ifdef LINUX_KERNEL
#define OutPortByte(p,v) outb((u8)(v),(p))
#define OutPortWord(p,v) outw((u16)(v),(p))
#define OutPortLong(p,v) outl((u32)(v),(p))
#define InPortByte(p)    inb(p)
#define InPortWord(p)    inw(p)
#define InPortLong(p)    inl(p)
#endif

/**********************************************************************/
/*  WIN 2000                                                          */
/**********************************************************************/

#ifdef WIN2000
#define OutPortByte(p,v) VideoPortWritePortUchar ((PUCHAR) (p), (UCHAR) (v))
#define OutPortWord(p,v) VideoPortWritePortUshort((PUSHORT) (p), (USHORT) (v))
#define OutPortLong(p,v) VideoPortWritePortUlong ((PULONG) (p), (ULONG) (v))
#define InPortByte(p)    VideoPortReadPortUchar  ((PUCHAR) (p))
#define InPortWord(p)    VideoPortReadPortUshort ((PUSHORT) (p))
#define InPortLong(p)    VideoPortReadPortUlong  ((PULONG) (p))
#endif


/**********************************************************************/
/*  WIN CE                                                          */
/**********************************************************************/

#ifdef WINCE_HEADER
#define OutPortByte(p,v) WRITE_PORT_UCHAR ((PUCHAR) (p), (UCHAR) (v))
#define OutPortWord(p,v) WRITE_PORT_USHORT((PUSHORT) (p), (USHORT) (v))
#define OutPortLong(p,v) WRITE_PORT_ULONG ((PULONG) (p), (ULONG) (v))
#define InPortByte(p)    READ_PORT_UCHAR  ((PUCHAR) (p))
#define InPortWord(p)    READ_PORT_USHORT ((PUSHORT) (p))
#define InPortLong(p)    READ_PORT_ULONG  ((PULONG) (p))
#endif
#endif // _OSDEF_H_
