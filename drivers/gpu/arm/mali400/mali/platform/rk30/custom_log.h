/*  --------------------------------------------------------------------------------------------------------
 *  File:   custom_log.h 
 *
 *  Desc:   ChenZhen 偏好的 log 输出的定制实现. 
 *
 *          -----------------------------------------------------------------------------------
 *          < 习语 和 缩略语 > : 
 *
 *          -----------------------------------------------------------------------------------
 *  Usage:		
 *
 *  Note:
 *
 *  Author: ChenZhen
 *
 *  --------------------------------------------------------------------------------------------------------
 *  Version:
 *          v1.0
 *  --------------------------------------------------------------------------------------------------------
 *  Log:
	----Fri Nov 19 15:20:28 2010            v1.0
 *        
 *  --------------------------------------------------------------------------------------------------------
 */


#ifndef __CUSTOM_LOG_H__
#define __CUSTOM_LOG_H__

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------------------------------------
 *  Include Files
 * ---------------------------------------------------------------------------------------------------------
 */
#include <linux/kernel.h>


/* ---------------------------------------------------------------------------------------------------------
 *  Macros Definition 
 * ---------------------------------------------------------------------------------------------------------
 */
    
/** 若下列 macro 有被定义, 才 使能 log 输出. */
#define ENABLE_DEBUG_LOG

/** .! : 若需要全局地关闭 D log, 可以使能下面的代码. */
/*
#undef ENABLE_DEBUG_LOG
#warning "custom debug log is disabled globally!"
*/

#define LOGD(fmt, args...) \
    printk(KERN_DEBUG fmt "\n", ## args)

/*---------------------------------------------------------------------------*/
    
#ifdef ENABLE_VERBOSE_LOG
/** Verbose log. */
#define V(fmt, args...) \
    { printk(KERN_DEBUG "V : [File] : %s; [Line] : %d; [Func] : %s(); " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }
#else
#define  V(...)  ((void)0)
#endif


#ifdef ENABLE_DEBUG_LOG
/** Debug log. */
#define D(fmt, args...) \
    { printk(KERN_DEBUG "D : [File] : %s; [Line] : %d; [Func] : %s(); " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }
#else
#define  D(...)  ((void)0)
#endif

#define I(fmt, args...) \
    { printk(KERN_INFO "I : [File] : %s; [Line] : %d; [Func] : %s(); " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }

#define W(fmt, args...) \
    { printk(KERN_WARNING "W : [File] : %s; [Line] : %d; [Func] : %s(); " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }

#define E(fmt, args...) \
    { printk(KERN_ERR "E : [File] : %s; [Line] : %d; [Func] : %s(); " fmt "\n", __FILE__, __LINE__, __FUNCTION__, ## args); }

/*-------------------------------------------------------*/

/** 使用 D(), 以十进制的形式打印变量 'var' 的 value. */
#define D_DEC(var)  D(#var " = %d.", var);

#define E_DEC(var)  E(#var " = %d.", var);

/** 使用 D(), 以十六进制的形式打印变量 'var' 的 value. */
#define D_HEX(var)  D(#var " = 0x%x.", var);

#define E_HEX(var)  E(#var " = 0x%x.", var);

/** 使用 D(), 以十六进制的形式 打印指针类型变量 'ptr' 的 value. */
#define D_PTR(ptr)  D(#ptr " = %p.", ptr);

#define E_PTR(ptr)  E(#ptr " = %p.", ptr);

/** 使用 D(), 打印 char 字串. */
#define D_STR(pStr) \
{\
    if ( NULL == pStr )\
    {\
        D(#pStr" = NULL.");\
    }\
    else\
    {\
        D(#pStr" = '%s'.", pStr);\
    }\
}

#define E_STR(pStr) \
{\
    if ( NULL == pStr )\
    {\
        E(#pStr" = NULL.");\
    }\
    else\
    {\
        E(#pStr" = '%s'.", pStr);\
    }\
}

#ifdef ENABLE_DEBUG_LOG
/**
 * log 从 'pStart' 地址开始的 'len' 个字节的数据. 
 */
#define D_MEM(pStart, len) \
    {\
        int i = 0;\
        char* p = (char*)pStart;\
        D("dump memory from addr of '" #pStart "', from %p, length %d' : ", pStart, len); \
        printk("\t\t");\
        for ( i = 0; i < len ; i++ )\
        {\
            printk("0x%02x, ", p[i] );\
        }\
        printk("\n");\
    }
#else
#define  D_MEM(...)  ((void)0)
#endif

/*-------------------------------------------------------*/

#define EXIT_FOR_DEBUG \
{\
    E("To exit for debug.");\
    return 1;\
}

/*-------------------------------------------------------*/

/**
 * 调用函数, 并检查返回值, 根据返回值决定是否跳转到指定的错误处理代码. 
 * @param functionCall
 *          对特定函数的调用, 该函数的返回值必须是 表征 成功 or err 的 整型数. 
 *          这里, 被调用函数 "必须" 是被定义为 "返回 0 表示操作成功". 
 * @param result
 *		    用于记录函数返回的 error code 的 整型变量, 通常是 "ret" or "result" 等.
 * @param label
 *		    若函数返回错误, 程序将要跳转到的 错误处理处的 标号, 通常就是 "EXIT". 
 */
#define CHECK_FUNC_CALL(functionCall, result, label) \
{\
	if ( 0 != ( (result) = (functionCall) ) )\
	{\
		E("Function call returned error : " #result " = %d.", result);\
		goto label;\
	}\
}

/**
 * 在特定条件下, 判定 error 发生, 对变量 'retVar' 设置 'errCode', 
 * Log 输出对应的 Error Caution, 然后跳转 'label' 指定的代码处执行. 
 * @param msg
 *          纯字串形式的提示信息. 
 * @param retVar
 *		    标识函数执行状态或者结果的变量, 将被设置具体的 Error Code. 
 *		    通常是 'ret' or 'result'. 
 * @param errCode
 *          表征特定 error 的常数标识, 通常是 宏的形态. 
 * @param label
 *          程序将要跳转到的错误处理代码的标号, 通常就是 'EXIT'. 
 * @param args...
 *          对应 'msgFmt' 实参中 '%s', '%d', ... 等 转换说明符 的具体可变长实参. 
 */
#define SET_ERROR_AND_JUMP(msgFmt, retVar, errCode, label, args...) \
{\
    E("To set '" #retVar "' to %d('" #errCode "'), because : " msgFmt, (errCode), ## args);\
	(retVar) = (errCode);\
	goto label;\
}


/* ---------------------------------------------------------------------------------------------------------
 *  Types and Structures Definition
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Global Functions' Prototype
 * ---------------------------------------------------------------------------------------------------------
 */


/* ---------------------------------------------------------------------------------------------------------
 *  Inline Functions Implementation 
 * ---------------------------------------------------------------------------------------------------------
 */

#ifdef __cplusplus
}
#endif

#endif /* __CUSTOM_LOG_H__ */

