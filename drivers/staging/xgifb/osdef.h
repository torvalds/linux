#ifndef _OSDEF_H_
#define _OSDEF_H_

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

#define OutPortByte(p,v) outb((u8)(v),(p))
#define OutPortWord(p,v) outw((u16)(v),(p))
#define OutPortLong(p,v) outl((u32)(v),(p))
#define InPortByte(p)    inb(p)
#define InPortWord(p)    inw(p)
#define InPortLong(p)    inl(p)

#endif // _OSDEF_H_
