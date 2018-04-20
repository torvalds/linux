/*
 * (C) COPYRIGHT RockChip Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 */

#ifndef __CUSTOM_LOG_H__
#define __CUSTOM_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* -----------------------------------------------------------------------------
 *  Include Files
 * -----------------------------------------------------------------------------
 */
#include <linux/kernel.h>
#include <linux/printk.h>

/* -----------------------------------------------------------------------------
 *  Macros Definition
 * -----------------------------------------------------------------------------
 */

/** 若下列 macro 有被定义, 才 使能 log 输出. */
/* #define ENABLE_DEBUG_LOG */

/*----------------------------------------------------------------------------*/

#ifdef ENABLE_VERBOSE_LOG
/** Verbose log. */
#define V(fmt, args...) \
	pr_debug("V : [File] : %s; [Line] : %d; [Func] : %s(); " fmt \
			"\n",	\
		__FILE__,	\
		__LINE__,	\
		__func__,	\
		## args)
#else
#define  V(...)  ((void)0)
#endif

#ifdef ENABLE_DEBUG_LOG
/** Debug log. */
#define D(fmt, args...) \
	pr_info("D : [File] : %s; [Line] : %d; [Func] : %s(); " fmt \
			"\n",	\
		__FILE__,	\
		__LINE__,	\
		__func__,	\
		## args)
#else
#define  D(...)  ((void)0)
#endif

#define I(fmt, args...) \
	pr_info("I : [File] : %s; [Line] : %d; [Func] : %s(); " fmt \
			"\n", \
		__FILE__, \
		__LINE__, \
		__func__, \
		## args)

#define W(fmt, args...) \
	pr_warn("W : [File] : %s; [Line] : %d; [Func] : %s(); " \
			fmt "\n", \
		__FILE__, \
		__LINE__, \
		__func__, \
		## args)

#define E(fmt, args...) \
	pr_err("E : [File] : %s; [Line] : %d; [Func] : %s(); " fmt \
			"\n", \
		__FILE__, \
		__LINE__, \
		__func__, \
		## args)

/*-------------------------------------------------------*/

/** 使用 D(), 以十进制的形式打印变量 'var' 的 value. */
#define D_DEC(var)  D(#var " = %d.", var)

#define E_DEC(var)  E(#var " = %d.", var)

/** 使用 D(), 以十六进制的形式打印变量 'var' 的 value. */
#define D_HEX(var)  D(#var " = 0x%x.", var)

#define E_HEX(var)  E(#var " = 0x%x.", var)

/**
 * 使用 D(), 以十六进制的形式,
 * 打印指针类型变量 'ptr' 的 value.
 */
#define D_PTR(ptr)  D(#ptr " = %p.", ptr)

#define E_PTR(ptr)  E(#ptr " = %p.", ptr)

/** 使用 D(), 打印 char 字串. */
#define D_STR(p_str) \
do { \
	if (!p_str) { \
		D(#p_str " = NULL."); \
	else \
		D(#p_str " = '%s'.", p_str); \
} while (0)

#define E_STR(p_str) \
do { \
	if (!p_str) \
		E(#p_str " = NULL."); \
	else \
		E(#p_str " = '%s'.", p_str); \
} while (0)

#ifdef ENABLE_DEBUG_LOG
/**
 * log 从 'p_start' 地址开始的 'len' 个字节的数据.
 */
#define D_MEM(p_start, len) \
do { \
	int i = 0; \
	char *p = (char *)(p_start); \
	D("dump memory from addr of '" #p_start "', from %p, length %d' : ", \
		(p_start), \
		(len)); \
	pr_debug("\t\t"); \
	for (i = 0; i < (len); i++) \
		pr_debug("0x%02x, ", p[i]); \
	pr_debug("\n"); \
} while (0)
#else
#define  D_MEM(...)  ((void)0)
#endif

/*-------------------------------------------------------*/

/**
 * 在特定条件下, 判定 error 发生,
 * 将变量 'ret_var' 设置 'err_code',
 * log 输出对应的 Error Caution,
 * 然后跳转 'label' 指定的代码处执行.
 * @param msg
 *	纯字串形式的提示信息.
 * @param ret_var
 *	标识函数执行状态或者结果的变量,
 *	将被设置具体的 Error Code.
 *	通常是 'ret' or 'result'.
 * @param err_code
 *	表征特定 error 的常数标识,
 *	通常是 宏的形态.
 * @param label
 *      程序将要跳转到的错误处理代码的标号,
 *      通常就是 'EXIT'.
 * @param args...
 *      对应 'msg_fmt' 实参中,
 *      '%s', '%d', ... 等转换说明符的具体可变长实参.
 */
#define SET_ERROR_AND_JUMP(msg_fmt, ret_var, err_code, label, args...) \
do { \
	E("To set '" #ret_var "' to %d('" #err_code "'), because : " msg_fmt, \
		(err_code), \
		## args); \
	(ret_var) = (err_code); \
	goto label; \
} while (0)

/* -----------------------------------------------------------------------------
 *  Types and Structures Definition
 * -----------------------------------------------------------------------------
 */

/* -----------------------------------------------------------------------------
 *  Global Functions' Prototype
 * -----------------------------------------------------------------------------
 */

/* -----------------------------------------------------------------------------
 *  Inline Functions Implementation
 * -----------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* __CUSTOM_LOG_H__ */
