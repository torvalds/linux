#ifdef __STANDBY_MODULE__
#include "pm_types.h" 
#include "pm.h"

void __div0(void)
{
	printk("Attempting division by 0!");
}

#elif defined(__KERNEL__)
#include <linux/module.h>
#endif

__u32 raw_lib_udiv(__u32 dividend, __u32 divisior)
{
    __u32   tmpDiv = (__u32)divisior;
    __u32   tmpQuot = 0;
    __s32   shift = 0;

    if(!divisior)
    {
        /* divide 0 error abort */
        return 0;
    }

    while(!(tmpDiv & ((__u32)1<<31)))
    {
        tmpDiv <<= 1;
        shift ++;
    }

    do
    {
        if(dividend >= tmpDiv)
        {
            dividend -= tmpDiv;
            tmpQuot = (tmpQuot << 1) | 1;
        }
        else
        {
            tmpQuot = (tmpQuot << 1) | 0;
        }
        tmpDiv >>= 1;
        shift --;
    } while(shift >= 0);

    return tmpQuot;
}
