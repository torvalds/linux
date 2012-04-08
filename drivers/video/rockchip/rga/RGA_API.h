#ifndef __RGA_API_H__
#define __RGA_API_H__

#include "rga_reg_info.h"
#include "rga.h"

#define ENABLE      1
#define DISABLE     0

int32_t RGA_gen_two_pro(struct rga_req *msg, struct rga_req *msg1);


#endif
