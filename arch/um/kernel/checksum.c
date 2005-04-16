#include "asm/uaccess.h"
#include "linux/errno.h"
#include "linux/module.h"

unsigned int arch_csum_partial(const unsigned char *buff, int len, int sum);

unsigned int csum_partial(unsigned char *buff, int len, int sum)
{
        return arch_csum_partial(buff, len, sum);
}

EXPORT_SYMBOL(csum_partial);

unsigned int csum_partial_copy_to(const unsigned char *src,
                                  unsigned char __user *dst, int len, int sum,
                                  int *err_ptr)
{
        if(copy_to_user(dst, src, len)){
                *err_ptr = -EFAULT;
                return(-1);
        }

        return(arch_csum_partial(src, len, sum));
}

unsigned int csum_partial_copy_from(const unsigned char __user *src,
                                    unsigned char *dst,	int len, int sum,
                                    int *err_ptr)
{
        if(copy_from_user(dst, src, len)){
                *err_ptr = -EFAULT;
                return(-1);
        }

        return arch_csum_partial(dst, len, sum);
}
