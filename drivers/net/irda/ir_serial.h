#ifndef _IR_SERIAL_H_
#define _IR_SERIAL_H_

#include "bu92725guw.h"

/*
 * define IOCTL macro
 */
/* magic number */
#define TTYIR_IOC_TYPE  0x56

/* TTYIR_STARTSEND */
/* function: start to send a frame in FIR mode */
/* parameter: no parameter */
/* return: no return code */
#define TTYIR_STARTSEND			   _IO(TTYIR_IOC_TYPE, 0)

/* TTYIR_GETLENGTH */
/* function: get the length of DATA field in the current frame */
/* paramter: no paramter */
/* return: the length of DATA field (unit:byte) */
#define TTYIR_GETLENGTH			   _IOR(TTYIR_IOC_TYPE, 1, unsigned long)

#endif
