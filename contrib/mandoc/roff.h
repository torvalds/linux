/*	$Id: roff.h,v 1.59 2018/04/11 17:11:13 schwarze Exp $	*/
/*
 * Copyright (c) 2008, 2009, 2010, 2011 Kristaps Dzonsons <kristaps@bsd.lv>
 * Copyright (c) 2013, 2014, 2015, 2017 Ingo Schwarze <schwarze@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHORS DISCLAIM ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

struct	ohash;
struct	mdoc_arg;
union	mdoc_data;

enum	roff_macroset {
	MACROSET_NONE = 0,
	MACROSET_MDOC,
	MACROSET_MAN
};

enum	roff_sec {
	SEC_NONE = 0,
	SEC_NAME,
	SEC_LIBRARY,
	SEC_SYNOPSIS,
	SEC_DESCRIPTION,
	SEC_CONTEXT,
	SEC_IMPLEMENTATION,	/* IMPLEMENTATION NOTES */
	SEC_RETURN_VALUES,
	SEC_ENVIRONMENT,
	SEC_FILES,
	SEC_EXIT_STATUS,
	SEC_EXAMPLES,
	SEC_DIAGNOSTICS,
	SEC_COMPATIBILITY,
	SEC_ERRORS,
	SEC_SEE_ALSO,
	SEC_STANDARDS,
	SEC_HISTORY,
	SEC_AUTHORS,
	SEC_CAVEATS,
	SEC_BUGS,
	SEC_SECURITY,
	SEC_CUSTOM,
	SEC__MAX
};

enum	roff_type {
	ROFFT_ROOT,
	ROFFT_BLOCK,
	ROFFT_HEAD,
	ROFFT_BODY,
	ROFFT_TAIL,
	ROFFT_ELEM,
	ROFFT_TEXT,
	ROFFT_COMMENT,
	ROFFT_TBL,
	ROFFT_EQN
};

enum	roff_tok {
	ROFF_br = 0,
	ROFF_ce,
	ROFF_ft,
	ROFF_ll,
	ROFF_mc,
	ROFF_po,
	ROFF_rj,
	ROFF_sp,
	ROFF_ta,
	ROFF_ti,
	ROFF_MAX,
	ROFF_ab,
	ROFF_ad,
	ROFF_af,
	ROFF_aln,
	ROFF_als,
	ROFF_am,
	ROFF_am1,
	ROFF_ami,
	ROFF_ami1,
	ROFF_as,
	ROFF_as1,
	ROFF_asciify,
	ROFF_backtrace,
	ROFF_bd,
	ROFF_bleedat,
	ROFF_blm,
	ROFF_box,
	ROFF_boxa,
	ROFF_bp,
	ROFF_BP,
	ROFF_break,
	ROFF_breakchar,
	ROFF_brnl,
	ROFF_brp,
	ROFF_brpnl,
	ROFF_c2,
	ROFF_cc,
	ROFF_cf,
	ROFF_cflags,
	ROFF_ch,
	ROFF_char,
	ROFF_chop,
	ROFF_class,
	ROFF_close,
	ROFF_CL,
	ROFF_color,
	ROFF_composite,
	ROFF_continue,
	ROFF_cp,
	ROFF_cropat,
	ROFF_cs,
	ROFF_cu,
	ROFF_da,
	ROFF_dch,
	ROFF_Dd,
	ROFF_de,
	ROFF_de1,
	ROFF_defcolor,
	ROFF_dei,
	ROFF_dei1,
	ROFF_device,
	ROFF_devicem,
	ROFF_di,
	ROFF_do,
	ROFF_ds,
	ROFF_ds1,
	ROFF_dwh,
	ROFF_dt,
	ROFF_ec,
	ROFF_ecr,
	ROFF_ecs,
	ROFF_el,
	ROFF_em,
	ROFF_EN,
	ROFF_eo,
	ROFF_EP,
	ROFF_EQ,
	ROFF_errprint,
	ROFF_ev,
	ROFF_evc,
	ROFF_ex,
	ROFF_fallback,
	ROFF_fam,
	ROFF_fc,
	ROFF_fchar,
	ROFF_fcolor,
	ROFF_fdeferlig,
	ROFF_feature,
	/* MAN_fi; ignored in mdoc(7) */
	ROFF_fkern,
	ROFF_fl,
	ROFF_flig,
	ROFF_fp,
	ROFF_fps,
	ROFF_fschar,
	ROFF_fspacewidth,
	ROFF_fspecial,
	ROFF_ftr,
	ROFF_fzoom,
	ROFF_gcolor,
	ROFF_hc,
	ROFF_hcode,
	ROFF_hidechar,
	ROFF_hla,
	ROFF_hlm,
	ROFF_hpf,
	ROFF_hpfa,
	ROFF_hpfcode,
	ROFF_hw,
	ROFF_hy,
	ROFF_hylang,
	ROFF_hylen,
	ROFF_hym,
	ROFF_hypp,
	ROFF_hys,
	ROFF_ie,
	ROFF_if,
	ROFF_ig,
	/* MAN_in; ignored in mdoc(7) */
	ROFF_index,
	ROFF_it,
	ROFF_itc,
	ROFF_IX,
	ROFF_kern,
	ROFF_kernafter,
	ROFF_kernbefore,
	ROFF_kernpair,
	ROFF_lc,
	ROFF_lc_ctype,
	ROFF_lds,
	ROFF_length,
	ROFF_letadj,
	ROFF_lf,
	ROFF_lg,
	ROFF_lhang,
	ROFF_linetabs,
	ROFF_lnr,
	ROFF_lnrf,
	ROFF_lpfx,
	ROFF_ls,
	ROFF_lsm,
	ROFF_lt,
	ROFF_mediasize,
	ROFF_minss,
	ROFF_mk,
	ROFF_mso,
	ROFF_na,
	ROFF_ne,
	/* MAN_nf; ignored in mdoc(7) */
	ROFF_nh,
	ROFF_nhychar,
	ROFF_nm,
	ROFF_nn,
	ROFF_nop,
	ROFF_nr,
	ROFF_nrf,
	ROFF_nroff,
	ROFF_ns,
	ROFF_nx,
	ROFF_open,
	ROFF_opena,
	ROFF_os,
	ROFF_output,
	ROFF_padj,
	ROFF_papersize,
	ROFF_pc,
	ROFF_pev,
	ROFF_pi,
	ROFF_PI,
	ROFF_pl,
	ROFF_pm,
	ROFF_pn,
	ROFF_pnr,
	ROFF_ps,
	ROFF_psbb,
	ROFF_pshape,
	ROFF_pso,
	ROFF_ptr,
	ROFF_pvs,
	ROFF_rchar,
	ROFF_rd,
	ROFF_recursionlimit,
	ROFF_return,
	ROFF_rfschar,
	ROFF_rhang,
	ROFF_rm,
	ROFF_rn,
	ROFF_rnn,
	ROFF_rr,
	ROFF_rs,
	ROFF_rt,
	ROFF_schar,
	ROFF_sentchar,
	ROFF_shc,
	ROFF_shift,
	ROFF_sizes,
	ROFF_so,
	ROFF_spacewidth,
	ROFF_special,
	ROFF_spreadwarn,
	ROFF_ss,
	ROFF_sty,
	ROFF_substring,
	ROFF_sv,
	ROFF_sy,
	ROFF_T_,
	ROFF_tc,
	ROFF_TE,
	ROFF_TH,
	ROFF_tkf,
	ROFF_tl,
	ROFF_tm,
	ROFF_tm1,
	ROFF_tmc,
	ROFF_tr,
	ROFF_track,
	ROFF_transchar,
	ROFF_trf,
	ROFF_trimat,
	ROFF_trin,
	ROFF_trnt,
	ROFF_troff,
	ROFF_TS,
	ROFF_uf,
	ROFF_ul,
	ROFF_unformat,
	ROFF_unwatch,
	ROFF_unwatchn,
	ROFF_vpt,
	ROFF_vs,
	ROFF_warn,
	ROFF_warnscale,
	ROFF_watch,
	ROFF_watchlength,
	ROFF_watchn,
	ROFF_wh,
	ROFF_while,
	ROFF_write,
	ROFF_writec,
	ROFF_writem,
	ROFF_xflag,
	ROFF_cblock,
	ROFF_RENAMED,
	ROFF_USERDEF,
	TOKEN_NONE,
	MDOC_Dd,
	MDOC_Dt,
	MDOC_Os,
	MDOC_Sh,
	MDOC_Ss,
	MDOC_Pp,
	MDOC_D1,
	MDOC_Dl,
	MDOC_Bd,
	MDOC_Ed,
	MDOC_Bl,
	MDOC_El,
	MDOC_It,
	MDOC_Ad,
	MDOC_An,
	MDOC_Ap,
	MDOC_Ar,
	MDOC_Cd,
	MDOC_Cm,
	MDOC_Dv,
	MDOC_Er,
	MDOC_Ev,
	MDOC_Ex,
	MDOC_Fa,
	MDOC_Fd,
	MDOC_Fl,
	MDOC_Fn,
	MDOC_Ft,
	MDOC_Ic,
	MDOC_In,
	MDOC_Li,
	MDOC_Nd,
	MDOC_Nm,
	MDOC_Op,
	MDOC_Ot,
	MDOC_Pa,
	MDOC_Rv,
	MDOC_St,
	MDOC_Va,
	MDOC_Vt,
	MDOC_Xr,
	MDOC__A,
	MDOC__B,
	MDOC__D,
	MDOC__I,
	MDOC__J,
	MDOC__N,
	MDOC__O,
	MDOC__P,
	MDOC__R,
	MDOC__T,
	MDOC__V,
	MDOC_Ac,
	MDOC_Ao,
	MDOC_Aq,
	MDOC_At,
	MDOC_Bc,
	MDOC_Bf,
	MDOC_Bo,
	MDOC_Bq,
	MDOC_Bsx,
	MDOC_Bx,
	MDOC_Db,
	MDOC_Dc,
	MDOC_Do,
	MDOC_Dq,
	MDOC_Ec,
	MDOC_Ef,
	MDOC_Em,
	MDOC_Eo,
	MDOC_Fx,
	MDOC_Ms,
	MDOC_No,
	MDOC_Ns,
	MDOC_Nx,
	MDOC_Ox,
	MDOC_Pc,
	MDOC_Pf,
	MDOC_Po,
	MDOC_Pq,
	MDOC_Qc,
	MDOC_Ql,
	MDOC_Qo,
	MDOC_Qq,
	MDOC_Re,
	MDOC_Rs,
	MDOC_Sc,
	MDOC_So,
	MDOC_Sq,
	MDOC_Sm,
	MDOC_Sx,
	MDOC_Sy,
	MDOC_Tn,
	MDOC_Ux,
	MDOC_Xc,
	MDOC_Xo,
	MDOC_Fo,
	MDOC_Fc,
	MDOC_Oo,
	MDOC_Oc,
	MDOC_Bk,
	MDOC_Ek,
	MDOC_Bt,
	MDOC_Hf,
	MDOC_Fr,
	MDOC_Ud,
	MDOC_Lb,
	MDOC_Lp,
	MDOC_Lk,
	MDOC_Mt,
	MDOC_Brq,
	MDOC_Bro,
	MDOC_Brc,
	MDOC__C,
	MDOC_Es,
	MDOC_En,
	MDOC_Dx,
	MDOC__Q,
	MDOC__U,
	MDOC_Ta,
	MDOC_MAX,
	MAN_TH,
	MAN_SH,
	MAN_SS,
	MAN_TP,
	MAN_LP,
	MAN_PP,
	MAN_P,
	MAN_IP,
	MAN_HP,
	MAN_SM,
	MAN_SB,
	MAN_BI,
	MAN_IB,
	MAN_BR,
	MAN_RB,
	MAN_R,
	MAN_B,
	MAN_I,
	MAN_IR,
	MAN_RI,
	MAN_nf,
	MAN_fi,
	MAN_RE,
	MAN_RS,
	MAN_DT,
	MAN_UC,
	MAN_PD,
	MAN_AT,
	MAN_in,
	MAN_OP,
	MAN_EX,
	MAN_EE,
	MAN_UR,
	MAN_UE,
	MAN_MT,
	MAN_ME,
	MAN_MAX
};

enum	roff_next {
	ROFF_NEXT_SIBLING = 0,
	ROFF_NEXT_CHILD
};

/*
 * Indicates that a BODY's formatting has ended, but
 * the scope is still open.  Used for badly nested blocks.
 */
enum	mdoc_endbody {
	ENDBODY_NOT = 0,
	ENDBODY_SPACE	/* Is broken: append a space. */
};

struct	roff_node {
	struct roff_node *parent;  /* Parent AST node. */
	struct roff_node *child;   /* First child AST node. */
	struct roff_node *last;    /* Last child AST node. */
	struct roff_node *next;    /* Sibling AST node. */
	struct roff_node *prev;    /* Prior sibling AST node. */
	struct roff_node *head;    /* BLOCK */
	struct roff_node *body;    /* BLOCK/ENDBODY */
	struct roff_node *tail;    /* BLOCK */
	struct mdoc_arg	 *args;    /* BLOCK/ELEM */
	union mdoc_data	 *norm;    /* Normalized arguments. */
	char		 *string;  /* TEXT */
	const struct tbl_span *span; /* TBL */
	struct eqn_box	 *eqn;     /* EQN */
	int		  line;    /* Input file line number. */
	int		  pos;     /* Input file column number. */
	int		  flags;
#define	NODE_VALID	 (1 << 0)  /* Has been validated. */
#define	NODE_ENDED	 (1 << 1)  /* Gone past body end mark. */
#define	NODE_EOS	 (1 << 2)  /* At sentence boundary. */
#define	NODE_LINE	 (1 << 3)  /* First macro/text on line. */
#define	NODE_SYNPRETTY	 (1 << 4)  /* SYNOPSIS-style formatting. */
#define	NODE_BROKEN	 (1 << 5)  /* Must validate parent when ending. */
#define	NODE_DELIMO	 (1 << 6)
#define	NODE_DELIMC	 (1 << 7)
#define	NODE_NOSRC	 (1 << 8)  /* Generated node, not in input file. */
#define	NODE_NOPRT	 (1 << 9)  /* Shall not print anything. */
	int		  prev_font; /* Before entering this node. */
	int		  aux;     /* Decoded node data, type-dependent. */
	enum roff_tok	  tok;     /* Request or macro ID. */
	enum roff_type	  type;    /* AST node type. */
	enum roff_sec	  sec;     /* Current named section. */
	enum mdoc_endbody end;     /* BODY */
};

struct	roff_meta {
	char		 *msec;    /* Manual section, usually a digit. */
	char		 *vol;     /* Manual volume title. */
	char		 *os;      /* Operating system. */
	char		 *arch;    /* Machine architecture. */
	char		 *title;   /* Manual title, usually CAPS. */
	char		 *name;    /* Leading manual name. */
	char		 *date;    /* Normalized date. */
	int		  hasbody; /* Document is not empty. */
	int		  rcsids;  /* Bits indexed by enum mandoc_os. */
	enum mandoc_os	  os_e;    /* Operating system. */
};

struct	roff_man {
	struct roff_meta  meta;    /* Document meta-data. */
	struct mparse	 *parse;   /* Parse pointer. */
	struct roff	 *roff;    /* Roff parser state data. */
	struct ohash	 *mdocmac; /* Mdoc macro lookup table. */
	struct ohash	 *manmac;  /* Man macro lookup table. */
	const char	 *os_s;    /* Default operating system. */
	struct roff_node *first;   /* The first node parsed. */
	struct roff_node *last;    /* The last node parsed. */
	struct roff_node *last_es; /* The most recent Es node. */
	int		  quick;   /* Abort parse early. */
	int		  flags;   /* Parse flags. */
#define	MDOC_LITERAL	 (1 << 1)  /* In a literal scope. */
#define	MDOC_PBODY	 (1 << 2)  /* In the document body. */
#define	MDOC_NEWLINE	 (1 << 3)  /* First macro/text in a line. */
#define	MDOC_PHRASE	 (1 << 4)  /* In a Bl -column phrase. */
#define	MDOC_PHRASELIT	 (1 << 5)  /* Literal within a phrase. */
#define	MDOC_FREECOL	 (1 << 6)  /* `It' invocation should close. */
#define	MDOC_SYNOPSIS	 (1 << 7)  /* SYNOPSIS-style formatting. */
#define	MDOC_KEEP	 (1 << 8)  /* In a word keep. */
#define	MDOC_SMOFF	 (1 << 9)  /* Spacing is off. */
#define	MDOC_NODELIMC	 (1 << 10) /* Disable closing delimiter handling. */
#define	MAN_ELINE	 (1 << 11) /* Next-line element scope. */
#define	MAN_BLINE	 (1 << 12) /* Next-line block scope. */
#define	MDOC_PHRASEQF	 (1 << 13) /* Quote first word encountered. */
#define	MDOC_PHRASEQL	 (1 << 14) /* Quote last word of this phrase. */
#define	MDOC_PHRASEQN	 (1 << 15) /* Quote first word of the next phrase. */
#define	MAN_LITERAL	  MDOC_LITERAL
#define	MAN_NEWLINE	  MDOC_NEWLINE
	enum roff_macroset macroset; /* Kind of high-level macros used. */
	enum roff_sec	  lastsec; /* Last section seen. */
	enum roff_sec	  lastnamed; /* Last standard section seen. */
	enum roff_next	  next;    /* Where to put the next node. */
};

extern	const char *const *roff_name;


void		 deroff(char **, const struct roff_node *);
struct ohash	*roffhash_alloc(enum roff_tok, enum roff_tok);
enum roff_tok	 roffhash_find(struct ohash *, const char *, size_t);
void		 roffhash_free(struct ohash *);
void		 roff_validate(struct roff_man *);
