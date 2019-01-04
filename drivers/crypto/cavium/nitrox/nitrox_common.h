/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NITROX_COMMON_H
#define __NITROX_COMMON_H

#include "nitrox_dev.h"
#include "nitrox_req.h"

int nitrox_crypto_register(void);
void nitrox_crypto_unregister(void);
void *crypto_alloc_context(struct nitrox_device *ndev);
void crypto_free_context(void *ctx);
struct nitrox_device *nitrox_get_first_device(void);
void nitrox_put_device(struct nitrox_device *ndev);

int nitrox_common_sw_init(struct nitrox_device *ndev);
void nitrox_common_sw_cleanup(struct nitrox_device *ndev);

void pkt_slc_resp_tasklet(unsigned long data);
int nitrox_process_se_request(struct nitrox_device *ndev,
			      struct se_crypto_request *req,
			      completion_t cb,
			      struct skcipher_request *skreq);
void backlog_qflush_work(struct work_struct *work);


#endif /* __NITROX_COMMON_H */
