#ifndef   _TV_CONF_H
#define   _TV_CONF_H

#include "tvoutc.h"
#include  <linux/amlogic/vout/vout_notify.h>


/*****************************************************************
**
**	type define part 
**
******************************************************************/
typedef  struct {
	unsigned int  major;  //dev major number
	const vinfo_t *vinfo;
	char 	     name[20] ;
	struct class  *base_class;
}disp_module_info_t ;

static  DEFINE_MUTEX(TV_mutex)  ;
/*****************************************************************
**
**	macro define part 
**
******************************************************************/
#define  TV_CLASS_NAME  	"tv"
#define	MAX_NUMBER_PARA  10

#define  SHOW_INFO(name)      \
	{return snprintf(buf,40, "%s\n", name);}  	

#define  STORE_INFO(name)\
	{mutex_lock(&TV_mutex);\
	snprintf(name,40,"%s",buf) ;\
	mutex_unlock(&TV_mutex); }			
		
#define    SET_TV_CLASS_ATTR(name,op)    \
static  char    name[40] ;				  \
static ssize_t aml_TV_attr_##name##_show(struct class  * cla, struct class_attribute *attr, char *buf)   \
{  											\
	SHOW_INFO(name)  	\
} 											\
static ssize_t  aml_TV_attr_##name##_store(struct class *cla,  struct class_attribute *attr, \
			    const char *buf, size_t count)    \
{\
	STORE_INFO(name);   						\
	op(name) ;						\
	return strnlen(buf, count);				\
}											\
struct  class_attribute  class_TV_attr_##name =  \
__ATTR(name, S_IRUGO|S_IWUSR, aml_TV_attr_##name##_show, aml_TV_attr_##name##_store) ; 

/*****************************************************************
**
**	function declare part 
**
******************************************************************/
tvmode_t vmode_to_tvmode(vmode_t mod);

#endif
