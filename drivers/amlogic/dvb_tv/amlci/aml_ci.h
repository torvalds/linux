#ifndef __AML_CI_H_
#define __AML_CI_H_

#include "../aml_dvb.h"
#include "aml_pcmcia.h"
#include "dvb_ca_en50221.h"


struct aml_ci {
	struct dvb_ca_en50221		en50221;
	struct aml_pcmcia			pc;
	struct mutex				ci_lock;
	int                                       ts;
	void						*priv;
	int                                       id;
	struct class                        class;
};

extern int aml_ci_init(struct platform_device *pdev, struct aml_dvb *dvb, struct aml_ci **cip);
extern void aml_ci_exit(struct aml_ci *ci);

#endif /* __AML_CI_H_ */

