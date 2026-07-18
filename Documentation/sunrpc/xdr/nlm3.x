/*
 * This file was extracted by hand from
 * https://pubs.opengroup.org/onlinepubs/9629799/chap10.htm#tagcjh_11_03
 */

/*
 * The NLMv3 protocol
 */

pragma header nlm3;

const LM_MAXSTRLEN = 1024;

const LM_MAXNAMELEN = 1025;

const MAXNETOBJ_SZ = 1024;

typedef opaque netobj<MAXNETOBJ_SZ>;

enum nlm_stats {
	LCK_GRANTED = 0,
	LCK_DENIED = 1,
	LCK_DENIED_NOLOCKS = 2,
	LCK_BLOCKED = 3,
	LCK_DENIED_GRACE_PERIOD = 4
};

pragma big_endian nlm_stats;

struct nlm_stat {
	nlm_stats	stat;
};

struct nlm_res {
	netobj		cookie;
	nlm_stat	stat;
};

struct nlm_holder {
	bool		exclusive;
	int		uppid;
	netobj		oh;
	unsigned int	l_offset;
	unsigned int	l_len;
};

union nlm_testrply switch (nlm_stats stat) {
	case LCK_DENIED:
		nlm_holder	holder;
	default:
		void;
};

struct nlm_testres {
	netobj		cookie;
	nlm_testrply	test_stat;
};

struct nlm_lock {
	string		caller_name<LM_MAXSTRLEN>;
	netobj		fh;
	netobj		oh;
	int		uppid;
	unsigned int	l_offset;
	unsigned int	l_len;
};

struct nlm_lockargs {
	netobj		cookie;
	bool		block;
	bool		exclusive;
	nlm_lock	alock;
	bool		reclaim;
	int		state;
};

struct nlm_cancargs {
	netobj		cookie;
	bool		block;
	bool		exclusive;
	nlm_lock	alock;
};

struct nlm_testargs {
	netobj		cookie;
	bool		exclusive;
	nlm_lock	alock;
};

struct nlm_unlockargs {
	netobj		cookie;
	nlm_lock	alock;
};

enum fsh_mode {
	fsm_DN = 0,
	fsm_DR = 1,
	fsm_DW = 2,
	fsm_DRW = 3
};

enum fsh_access {
	fsa_NONE = 0,
	fsa_R = 1,
	fsa_W = 2,
	fsa_RW = 3
};

struct nlm_share {
	string		caller_name<LM_MAXSTRLEN>;
	netobj		fh;
	netobj		oh;
	fsh_mode	mode;
	fsh_access	access;
};

struct nlm_shareargs {
	netobj		cookie;
	nlm_share	share;
	bool		reclaim;
};

struct nlm_shareres {
	netobj		cookie;
	nlm_stats	stat;
	int		sequence;
};

struct nlm_notify {
	string		name<LM_MAXNAMELEN>;
	long		state;
};

/*
 * Argument for the Linux-private SM_NOTIFY procedure
 */
const SM_PRIV_SIZE = 16;

struct nlm_notifyargs {
	nlm_notify	notify;
	opaque		private[SM_PRIV_SIZE];
};

program NLM_PROG {
	version NLM_VERS {
		void		NLM_NULL(void)				= 0;
		nlm_testres	NLM_TEST(nlm_testargs)			= 1;
		nlm_res		NLM_LOCK(nlm_lockargs)			= 2;
		nlm_res		NLM_CANCEL(nlm_cancargs)		= 3;
		nlm_res		NLM_UNLOCK(nlm_unlockargs)		= 4;
		nlm_res		NLM_GRANTED(nlm_testargs)		= 5;
		void		NLM_TEST_MSG(nlm_testargs)		= 6;
		void		NLM_LOCK_MSG(nlm_lockargs)		= 7;
		void		NLM_CANCEL_MSG(nlm_cancargs)		= 8;
		void		NLM_UNLOCK_MSG(nlm_unlockargs)		= 9;
		void		NLM_GRANTED_MSG(nlm_testargs)		= 10;
		void		NLM_TEST_RES(nlm_testres)		= 11;
		void		NLM_LOCK_RES(nlm_res)			= 12;
		void		NLM_CANCEL_RES(nlm_res)			= 13;
		void		NLM_UNLOCK_RES(nlm_res)			= 14;
		void		NLM_GRANTED_RES(nlm_res)		= 15;
		void		NLM_SM_NOTIFY(nlm_notifyargs)		= 16;
		nlm_shareres	NLM_SHARE(nlm_shareargs)		= 20;
		nlm_shareres	NLM_UNSHARE(nlm_shareargs)		= 21;
		nlm_res		NLM_NM_LOCK(nlm_lockargs)		= 22;
		void		NLM_FREE_ALL(nlm_notify)		= 23;
	} = 3;
} = 100021;
