/**
 ******************************************************************************
 *
 * @file ecrnx_amt.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ECRNX_AMT_H_
#define _ECRNX_AMT_H_

#ifdef CONFIG_ECRNX_WIFO_CAIL
int ecrnx_amt_init(void);
void ecrnx_amt_deinit(void);

extern struct ecrnx_iwpriv_amt_vif amt_vif;
#endif

#endif /* _ECRNX_AMT_H_ */
