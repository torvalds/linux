/*
 * Some helper stuff shared between the sample programs.
 */
#ifndef __TESTHELPER_H
#define __TESTHELPER_H

#define buf_align(buf, align) (void *)(((unsigned long)(buf) + (align)) & ~(align))

#endif /* __TESTHELPER_H */
