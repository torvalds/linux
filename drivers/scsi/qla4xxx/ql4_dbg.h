/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2012 QLogic Corporation
 */

/*
 * Driver debug definitions.
 */
/* #define QL_DEBUG  */			/* DEBUG messages */
/* #define QL_DEBUG_LEVEL_3  */		/* Output function tracing */
/* #define QL_DEBUG_LEVEL_4  */
/* #define QL_DEBUG_LEVEL_5  */
/* #define QL_DEBUG_LEVEL_7  */
/* #define QL_DEBUG_LEVEL_9  */

#define QL_DEBUG_LEVEL_2	/* ALways enable error messagess */
#if defined(QL_DEBUG)
#define DEBUG(x)   do {x;} while (0);
#else
#define DEBUG(x)	do {} while (0);
#endif

#if defined(QL_DEBUG_LEVEL_2)
#define DEBUG2(x)      do {if(ql4xextended_error_logging == 2) x;} while (0);
#define DEBUG2_3(x)   do {x;} while (0);
#else				/*  */
#define DEBUG2(x)	do {} while (0);
#endif				/*  */

#if defined(QL_DEBUG_LEVEL_3)
#define DEBUG3(x)      do {if(ql4xextended_error_logging == 3) x;} while (0);
#else				/*  */
#define DEBUG3(x)	do {} while (0);
#if !defined(QL_DEBUG_LEVEL_2)
#define DEBUG2_3(x)	do {} while (0);
#endif				/*  */
#endif				/*  */
#if defined(QL_DEBUG_LEVEL_4)
#define DEBUG4(x)	do {x;} while (0);
#else				/*  */
#define DEBUG4(x)	do {} while (0);
#endif				/*  */

#if defined(QL_DEBUG_LEVEL_5)
#define DEBUG5(x)	do {x;} while (0);
#else				/*  */
#define DEBUG5(x)	do {} while (0);
#endif				/*  */

#if defined(QL_DEBUG_LEVEL_7)
#define DEBUG7(x)	do {x; } while (0)
#else				/*  */
#define DEBUG7(x)	do {} while (0)
#endif				/*  */

#if defined(QL_DEBUG_LEVEL_9)
#define DEBUG9(x)	do {x;} while (0);
#else				/*  */
#define DEBUG9(x)	do {} while (0);
#endif				/*  */
