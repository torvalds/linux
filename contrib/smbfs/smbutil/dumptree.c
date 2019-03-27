/* $FreeBSD$ */

#include <sys/param.h>
#include <sys/time.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef APPLE
#include <err.h>
#include <sysexits.h>
#endif

#include <netsmb/smb_lib.h>
#include <netsmb/smb_conn.h>

#include "common.h"

#define	DEFBIT(bit)	{bit, #bit}

static struct smb_bitname conn_caps[] = {
	DEFBIT(SMB_CAP_RAW_MODE),
	DEFBIT(SMB_CAP_MPX_MODE),
	DEFBIT(SMB_CAP_UNICODE),
	DEFBIT(SMB_CAP_LARGE_FILES),
	DEFBIT(SMB_CAP_NT_SMBS),
	DEFBIT(SMB_CAP_NT_FIND),
	DEFBIT(SMB_CAP_EXT_SECURITY),
	{0, NULL}
};

static struct smb_bitname vc_flags[] = {
	DEFBIT(SMBV_PERMANENT),
	{SMBV_PRIVATE,	"private"},
	{SMBV_SINGLESHARE, "singleshare"},
	{SMBV_ENCRYPT,	"encpwd"},
	{SMBV_WIN95,	"win95"},
	{SMBV_LONGNAMES,"longnames"},
	{0, NULL}
};

static struct smb_bitname ss_flags[] = {
	DEFBIT(SMBS_PERMANENT),
	{0, NULL}
};

static char *conn_proto[] = {
	"unknown",
	"PC NETWORK PROGRAM 1.0, PCLAN1.0",
	"MICROSOFT NETWORKS 1.03",
	"MICROSOFT NETWORKS 3.0, LANMAN1.0",
	"LM1.2X002, DOS LM1.2X002",
	"DOS LANMAN2.1, LANMAN2.1",
	"NT LM 0.12, Windows for Workgroups 3.1a, NT LANMAN 1.0"
};

static char *iod_state[] = {
	"Not connected",
	"Reconnecting",
	"Transport activated",
	"Session active",
	"Session dead"
};

static void
print_vcinfo(struct smb_vc_info *vip)
{
	char buf[200];

	printf("VC: \\\\%s\\%s\n", vip->srvname, vip->vcname);
	printf("(%s:%s) %o", user_from_uid(vip->uid, 0), 
	    group_from_gid(vip->gid, 0), vip->mode);
	printf("\n");
	if (!verbose)
		return;
	iprintf(4, "state:    %s\n", iod_state[vip->iodstate]);
	iprintf(4, "flags:    0x%04x %s\n", vip->flags,
	    smb_printb(buf, vip->flags, vc_flags));
	iprintf(4, "usecount: %d\n", vip->usecount);
	iprintf(4, "dialect:  %d (%s)\n", vip->sopt.sv_proto, conn_proto[vip->sopt.sv_proto]);
	iprintf(4, "smode:    %d\n", vip->sopt.sv_sm);
	iprintf(4, "caps:     0x%04x %s\n", vip->sopt.sv_caps,
	    smb_printb(buf, vip->sopt.sv_caps, conn_caps));
	iprintf(4, "maxmux:   %d\n", vip->sopt.sv_maxmux);
	iprintf(4, "maxvcs:   %d\n", vip->sopt.sv_maxvcs);
}

static void
print_shareinfo(struct smb_share_info *sip)
{
	char buf[200];

	iprintf(4, "Share:    %s", sip->sname);
	printf("(%s:%s) %o", user_from_uid(sip->uid, 0), 
	    group_from_gid(sip->gid, 0), sip->mode);
	printf("\n");
	if (!verbose)
		return;
	iprintf(8, "flags:    0x%04x %s\n", sip->flags,
	    smb_printb(buf, sip->flags, ss_flags));
	iprintf(8, "usecount: %d\n", sip->usecount);
}

int
cmd_dumptree(int argc, char *argv[])
{
	void *p, *op;
	int *itype;

	printf("SMB connections:\n");
#ifdef APPLE
	if (loadsmbvfs())
		errx(EX_OSERR, "SMB filesystem is not available");
#endif
	p = smb_dumptree();
	if (p == NULL) {
		printf("None\n");
		return 0;
	}
	op = p;
	for (;;) {
		itype = p;
		if (*itype == SMB_INFO_NONE)
			break;
		switch (*itype) {
		    case SMB_INFO_VC:
			print_vcinfo(p);
			p = (struct smb_vc_info*)p + 1;
			break;
		    case SMB_INFO_SHARE:
			print_shareinfo(p);
			p = (struct smb_share_info*)p + 1;
			break;
		    default:
			printf("Out of sync\n");
			free(op);
			return 1;
		    
		}
	}
	free(op);
	printf("\n");
	return 0;
}
