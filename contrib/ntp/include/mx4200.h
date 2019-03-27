
/* records transmitted from extern CDU to MX 4200 */
#define PMVXG_S_INITMODEA	0	/* initialization/mode part A */
#define PMVXG_S_INITMODEB	1	/* initialization/mode part B*/
#define PMVXG_S_SATHEALTH	2	/* satellite health control */
#define PMVXG_S_DIFFNAV		3	/* differential navigation control */
#define PMVXG_S_PORTCONF	7	/* control port configuration */
#define PMVXG_S_GETSELFTEST	13	/* self test (request results) */
#define PMVXG_S_RTCMCONF	16	/* RTCM port configuration */
#define PMVXG_S_PASSTHRU	17	/* equipment port pass-thru config */
#define PMVXG_S_RESTART		18	/* restart control */
#define PMVXG_S_OSCPARAM	19	/* oscillator parameter */
#define PMVXG_S_DOSELFTEST	20	/* self test (activate a test) */
#define PMVXG_S_TRECOVCONF	23	/* time recovery configuration */
#define PMVXG_S_RAWDATASEL	24	/* raw data port data selection */
#define PMVXG_S_EQUIPCONF	26	/* equipment port configuration */
#define PMVXG_S_RAWDATACONF	27	/* raw data port configuration */

/* records transmitted from MX 4200 to external CDU */
#define PMVXG_D_STATUS		0	/* status */
#define PMVXG_D_POSITION	1	/* position */
#define PMVXG_D_OPDOPS		3	/* (optimum) DOPs */
#define PMVXG_D_MODEDATA	4	/* mode data */
#define PMVXG_D_SATPRED		5	/* satellite predictions */
#define PMVXG_D_SATHEALTH	6	/* satellite health status */
#define PMVXG_D_UNRECOG		7	/* unrecognized request response */
#define PMVXG_D_SIGSTRLOC	8	/* sig strength & location (sats 1-4) */
#define PMVXG_D_SPEEDHEAD	11	/* speed/heading data */
#define PMVXG_D_OSELFTEST	12	/* (old) self-test results */
#define PMVXG_D_SIGSTRLOC2	18	/* sig strength & location (sats 5-8) */
#define PMVXG_D_OSCPARAM	19	/* oscillator parameter */
#define PMVXG_D_SELFTEST	20	/* self test results */
#define PMVXG_D_PHV		21	/* position, height & velocity */
#define PMVXG_D_DOPS		22	/* DOPs */
#define PMVXG_D_SOFTCONF	30	/* software configuration */
#define PMVXG_D_DIFFGPSMODE	503	/* differential gps moding */
#define PMVXG_D_TRECOVUSEAGE	523	/* time recovery usage */
#define PMVXG_D_RAWDATAOUT	524	/* raw data port data output */
#define PMVXG_D_TRECOVRESULT	828	/* time recovery results */
#define PMVXG_D_TRECOVOUT	830	/* time recovery output message */
