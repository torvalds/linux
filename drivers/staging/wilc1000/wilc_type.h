/* ////////////////////////////////////////////////////////////////////////// */
/*  */
/* Copyright (c) Atmel Corporation.  All rights reserved. */
/*  */
/* Module Name:  wilc_type.h */
/*  */
/*  */
/* //////////////////////////////////////////////////////////////////////////// */
#ifndef WILC_TYPE_H
#define WILC_TYPE_H

/********************************************
 *
 *      Type Defines
 *
 ********************************************/
#ifdef WIN32
typedef char int8_t;
typedef short int16_t;
typedef long int32_t;
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned long uint32_t;
#else
#ifdef _linux_
/*typedef unsigned char         uint8_t;
 * typedef unsigned short       uint16_t;
 * typedef unsigned long        uint32_t;*/
#include <stdint.h>
#else
#include "wilc_oswrapper.h"
#endif
#endif
#endif
