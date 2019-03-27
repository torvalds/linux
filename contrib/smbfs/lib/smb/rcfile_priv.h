
struct rckey {
	SLIST_ENTRY(rckey)	rk_next;
	char 			*rk_name;
	char			*rk_value;
};

struct rcsection {
	SLIST_ENTRY(rcsection)	rs_next;
	SLIST_HEAD(rckey_head,rckey) rs_keys;
	char			*rs_name;
};
    
struct rcfile {
	SLIST_ENTRY(rcfile)	rf_next;
	SLIST_HEAD(rcsec_head, rcsection) rf_sect;
	char			*rf_name;
	FILE			*rf_f;
};

