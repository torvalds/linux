/***********************************************************************
**
** Implementation of the Skein hash function.
**
** Source code author: Doug Whiting, 2008.
**
** This algorithm and source code is released to the public domain.
**
************************************************************************/
#ifndef _SKEIN_BLOCK_H_
#define _SKEIN_BLOCK_H_

#include <skein.h> /* get the Skein API definitions   */

void Skein_256_Process_Block(struct skein_256_ctx *ctx, const u8 *blkPtr,
				size_t blkCnt, size_t byteCntAdd);
void Skein_512_Process_Block(struct skein_512_ctx *ctx, const u8 *blkPtr,
				size_t blkCnt, size_t byteCntAdd);
void Skein1024_Process_Block(struct skein1024_ctx *ctx, const u8 *blkPtr,
				size_t blkCnt, size_t byteCntAdd);

#endif
