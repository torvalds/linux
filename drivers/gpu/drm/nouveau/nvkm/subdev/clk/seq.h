/* SPDX-License-Identifier: MIT */
#ifndef __NVKM_CLK_SEQ_H__
#define __NVKM_CLK_SEQ_H__
#include <subdev/bus/hwsq.h>

#define clk_init(s,p)       hwsq_init(&(s)->base, (p))
#define clk_exec(s,e)       hwsq_exec(&(s)->base, (e))
#define clk_have(s,r)       ((s)->r_##r.addr != 0x000000)
#define clk_rd32(s,r)       hwsq_rd32(&(s)->base, &(s)->r_##r)
#define clk_wr32(s,r,d)     hwsq_wr32(&(s)->base, &(s)->r_##r, (d))
#define clk_mask(s,r,m,d)   hwsq_mask(&(s)->base, &(s)->r_##r, (m), (d))
#define clk_setf(s,f,d)     hwsq_setf(&(s)->base, (f), (d))
#define clk_wait(s,f,d)     hwsq_wait(&(s)->base, (f), (d))
#define clk_nsec(s,n)       hwsq_nsec(&(s)->base, (n))
#endif
