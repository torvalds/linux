#ifndef __QCOM_WNCSS_H__
#define __QCOM_WNCSS_H__

struct qcom_iris;
struct qcom_wcnss;

struct wcnss_vreg_info {
	const char * const name;
	int min_voltage;
	int max_voltage;

	int load_uA;

	bool super_turbo;
};

int qcom_iris_enable(struct qcom_iris *iris);
void qcom_iris_disable(struct qcom_iris *iris);

void qcom_wcnss_assign_iris(struct qcom_wcnss *wcnss, struct qcom_iris *iris, bool use_48mhz_xo);

#endif
