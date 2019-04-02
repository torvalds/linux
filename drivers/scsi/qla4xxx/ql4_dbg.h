/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2012 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

/*
 * Driver de definitions.
 */
/* #define QL_DE  */			/* DE messages */
/* #define QL_DE_LEVEL_3  */		/* Output function tracing */
/* #define QL_DE_LEVEL_4  */
/* #define QL_DE_LEVEL_5  */
/* #define QL_DE_LEVEL_7  */
/* #define QL_DE_LEVEL_9  */

#define QL_DE_LEVEL_2	/* ALways enable error messagess */
#if defined(QL_DE)
#define DE(x)   do {x;} while (0);
#else
#define DE(x)	do {} while (0);
#endif

#if defined(QL_DE_LEVEL_2)
#define DE2(x)      do {if(ql4xextended_error_logging == 2) x;} while (0);
#define DE2_3(x)   do {x;} while (0);
#else				/*  */
#define DE2(x)	do {} while (0);
#endif				/*  */

#if defined(QL_DE_LEVEL_3)
#define DE3(x)      do {if(ql4xextended_error_logging == 3) x;} while (0);
#else				/*  */
#define DE3(x)	do {} while (0);
#if !defined(QL_DE_LEVEL_2)
#define DE2_3(x)	do {} while (0);
#endif				/*  */
#endif				/*  */
#if defined(QL_DE_LEVEL_4)
#define DE4(x)	do {x;} while (0);
#else				/*  */
#define DE4(x)	do {} while (0);
#endif				/*  */

#if defined(QL_DE_LEVEL_5)
#define DE5(x)	do {x;} while (0);
#else				/*  */
#define DE5(x)	do {} while (0);
#endif				/*  */

#if defined(QL_DE_LEVEL_7)
#define DE7(x)	do {x; } while (0)
#else				/*  */
#define DE7(x)	do {} while (0)
#endif				/*  */

#if defined(QL_DE_LEVEL_9)
#define DE9(x)	do {x;} while (0);
#else				/*  */
#define DE9(x)	do {} while (0);
#endif				/*  */
