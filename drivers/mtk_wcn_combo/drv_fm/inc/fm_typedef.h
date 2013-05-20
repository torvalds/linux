#ifndef __FM_TYPEDEF_H__
#define __FM_TYPEDEF_H__

typedef signed char fm_s8;
typedef signed short fm_s16;
typedef signed int fm_s32;
typedef signed long long fm_s64;
typedef unsigned char fm_u8;
typedef unsigned short fm_u16;
typedef unsigned int fm_u32;
typedef unsigned long long fm_u64;

#ifndef FALSE
#define FALSE (0)
#endif

#ifndef TRUE
#define TRUE  (1)
#endif

#ifndef NULL
#define NULL  (0)
#endif

#ifndef BOOL
typedef unsigned char  BOOL;
#endif

typedef enum fm_bool {
    fm_false = 0,
    fm_true  = 1
} fm_bool;


#endif //__FM_TYPEDEF_H__

