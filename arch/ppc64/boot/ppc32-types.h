#ifndef _PPC64_TYPES_H
#define _PPC64_TYPES_H

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

typedef __signed__ long long __s64;
typedef unsigned long long __u64;

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long long s64;
typedef unsigned long long u64;

typedef struct {
	__u32 u[4];
} __attribute((aligned(16))) __vector128;

#define BITS_PER_LONG 32

typedef __vector128 vector128;

#endif /* _PPC64_TYPES_H */
