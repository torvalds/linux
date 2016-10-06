#include "qed.h"
#include "qed_dev_api.h"
#include "qed_mcp.h"
#include "qed_sp.h"
#include "qed_selftest.h"

int qed_selftest_memory(struct qed_dev *cdev)
{
	int rc = 0, i;

	for_each_hwfn(cdev, i) {
		rc = qed_sp_heartbeat_ramrod(&cdev->hwfns[i]);
		if (rc)
			return rc;
	}

	return rc;
}

int qed_selftest_interrupt(struct qed_dev *cdev)
{
	int rc = 0, i;

	for_each_hwfn(cdev, i) {
		rc = qed_sp_heartbeat_ramrod(&cdev->hwfns[i]);
		if (rc)
			return rc;
	}

	return rc;
}

int qed_selftest_register(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn;
	struct qed_ptt *p_ptt;
	int rc = 0, i;

	/* although performed by MCP, this test is per engine */
	for_each_hwfn(cdev, i) {
		p_hwfn = &cdev->hwfns[i];
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt) {
			DP_ERR(p_hwfn, "failed to acquire ptt\n");
			return -EBUSY;
		}
		rc = qed_mcp_bist_register_test(p_hwfn, p_ptt);
		qed_ptt_release(p_hwfn, p_ptt);
		if (rc)
			break;
	}

	return rc;
}

int qed_selftest_clock(struct qed_dev *cdev)
{
	struct qed_hwfn *p_hwfn;
	struct qed_ptt *p_ptt;
	int rc = 0, i;

	/* although performed by MCP, this test is per engine */
	for_each_hwfn(cdev, i) {
		p_hwfn = &cdev->hwfns[i];
		p_ptt = qed_ptt_acquire(p_hwfn);
		if (!p_ptt) {
			DP_ERR(p_hwfn, "failed to acquire ptt\n");
			return -EBUSY;
		}
		rc = qed_mcp_bist_clock_test(p_hwfn, p_ptt);
		qed_ptt_release(p_hwfn, p_ptt);
		if (rc)
			break;
	}

	return rc;
}
