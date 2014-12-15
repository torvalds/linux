#ifndef   _VOUT_SERVE_H
#define	_VOUT_SERVE_H

#ifdef CONFIG_AM_TV_OUTPUT
#include  "tvoutc.h"	
#endif
#include  <linux/amlogic/vout/vout_notify.h>

/*****************************************************************
**
**	type define part 
**
******************************************************************/

static  DEFINE_MUTEX(vout_mutex)  ;

typedef enum   {
VOUT_ATTR_ENABLE=0 ,
VOUT_ATTR_MODE,
VOUT_ATTR_AXIS,
VOUT_ATTR_WR_REG,
VOUT_ATTR_RD_REG,
VOUT_ATTR_MAX
}vout_attr_t ;

typedef  struct {
	unsigned int  addr;
	unsigned int  value;
}vout_reg_t ;

typedef  struct {
	int x ;
	int y ;
	int w ;
	int h ;
}disp_rect_t;
typedef struct {
	 struct class  *base_class;
}vout_info_t;
/*****************************************************************
**
**	macro define part 
**
******************************************************************/
#define  VOUT_CLASS_NAME  	"display"
#define	MAX_NUMBER_PARA  10

#define  SHOW_INFO(name)      \
	{return snprintf(buf,40, "%s\n", name);}  	

#define  STORE_INFO(name)\
	{mutex_lock(&vout_mutex);\
	snprintf(name,40,"%s",buf) ;\
	mutex_unlock(&vout_mutex); }			
		
#define    SET_VOUT_CLASS_ATTR(name,op)    \
static  char    name[40] ;				  \
static ssize_t aml_vout_attr_##name##_show(struct class  * cla, struct class_attribute *attr, char *buf)   \
{  											\
	SHOW_INFO(name)  	\
} 											\
static ssize_t  aml_vout_attr_##name##_store(struct class *cla,  struct class_attribute *attr, \
			    const char *buf, size_t count)    \
{\
	STORE_INFO(name);   						\
	op(name) ;						\
	return strnlen(buf, count);				\
}											\
struct  class_attribute  class_vout_attr_##name =  \
__ATTR(name, S_IRUGO|S_IWUSR|S_IWGRP, aml_vout_attr_##name##_show, aml_vout_attr_##name##_store) ; 
/*****************************************************************
**
**	function  declare  part 
**
******************************************************************/
static  void  read_reg(char *para);
static  void  write_reg(char *para);
static  void  set_vout_mode(char *mode) ;
static void  set_vout_window(char *para) ;
static  void   func_default_null(char  *str);


#endif
