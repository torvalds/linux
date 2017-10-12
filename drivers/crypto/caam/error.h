/*
 * CAAM Error Reporting code header
 *
 * Copyright 2009-2011 Freescale Semiconductor, Inc.
 */

#ifndef CAAM_ERROR_H
#define CAAM_ERROR_H
#define CAAM_ERROR_STR_MAX 302
void caam_jr_strstatus(struct device *jrdev, u32 status);

void caam_dump_sg(const char *level, const char *prefix_str, int prefix_type,
		  int rowsize, int groupsize, struct scatterlist *sg,
		  size_t tlen, bool ascii);
#endif /* CAAM_ERROR_H */
