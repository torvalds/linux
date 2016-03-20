#ifndef _VM_BASIC_TYPES_H_
#define _VM_BASIC_TYPES_H_
#include <linux/kernel.h>

typedef u32 uint32;
typedef s32 int32;
typedef u64 uint64;
typedef u16 uint16;
typedef s16 int16;
typedef u8  uint8;
typedef s8  int8;

typedef uint64 PA;
typedef uint32 PPN;
typedef uint64 PPN64;

typedef bool Bool;

#define MAX_UINT32 U32_MAX

#endif
