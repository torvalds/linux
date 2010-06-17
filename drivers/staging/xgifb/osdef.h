#ifndef _OSDEF_H_
#define _OSDEF_H_

#define LINUX_KERNEL

/**********************************************************************/
#ifdef LINUX_KERNEL
//#include <linux/config.h>
#endif


/**********************************************************************/
#ifdef LINUX_KERNEL
#define LINUX
#endif

/**********************************************************************/
#ifdef LINUX_KERNEL
#define XGI_SetMemory(MemoryAddress,MemorySize,value) memset(MemoryAddress, value, MemorySize)
#endif
/**********************************************************************/

/**********************************************************************/

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


/**********************************************************************/
/*  LINUX XF86                                                        */
/**********************************************************************/


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



/**********************************************************************/
/*  WIN CE                                                          */
/**********************************************************************/

#endif // _OSDEF_H_
