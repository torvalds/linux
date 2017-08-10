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

void nitrox_pf_cleanup_isr(struct nitrox_device *ndev);
int nitrox_pf_init_isr(struct nitrox_device *ndev);

int nitrox_common_sw_init(struct nitrox_device *ndev);
void nitrox_common_sw_cleanup(struct nitrox_device *ndev);

void pkt_slc_resp_handler(unsigned long data);
int nitrox_process_se_request(struct nitrox_device *ndev,
			      struct se_crypto_request *req,
			      completion_t cb,
			      struct skcipher_request *skreq);
void backlog_qflush_work(struct work_struct *work);

void nitrox_config_emu_unit(struct nitrox_device *ndev);
void nitrox_config_pkt_input_rings(struct nitrox_device *ndev);
void nitrox_config_pkt_solicit_ports(struct nitrox_device *ndev);
void nitrox_config_vfmode(struct nitrox_device *ndev, int mode);
void nitrox_config_nps_unit(struct nitrox_device *ndev);
void nitrox_config_pom_unit(struct nitrox_device *ndev);
void nitrox_config_rand_unit(struct nitrox_device *ndev);
void nitrox_config_efl_unit(struct nitrox_device *ndev);
void nitrox_config_bmi_unit(struct nitrox_device *ndev);
void nitrox_config_bmo_unit(struct nitrox_device *ndev);
void nitrox_config_lbc_unit(struct nitrox_device *ndev);
void invalidate_lbc(struct nitrox_device *ndev);
void enable_pkt_input_ring(struct nitrox_device *ndev, int ring);
void enable_pkt_solicit_port(struct nitrox_device *ndev, int port);

#endif /* __NITROX_COMMON_H */
