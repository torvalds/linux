/*
 * Copyright (c) 1997-2014 Erez Zadok
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989 The Regents of the University of California.
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *
 * File: am-utils/fsinfo/fsi_gram.y
 *
 */

%{
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */
#include <am_defs.h>
#include <fsi_data.h>
#include <fsinfo.h>

extern qelem *list_of_hosts, *list_of_automounts;
%}

%union {
	auto_tree *a;
	disk_fs *d;
	ether_if *e;
	host *h;
	qelem *q;
	char *s;
	fsi_mount *m;
	fsmount *f;
}

%token	tARCH
%token	tAS
%token	tAUTOMOUNT
%token	tCLUSTER
%token	tCONFIG
%token	tDUMPSET
%token	tEQ
%token  tNFSEQ
%token	tEXPORTFS
%token	tFREQ
%token	tFROM
%token	tFS
%token	tFSTYPE
%token	tHWADDR
%token	tINADDR
%token	tHOST
%token	tLOCALHOST
%token	tLOG
%token	tMOUNT
%token	tNETMASK
%token	tNETIF
%token	tVOLNAME
%token	tOPTS
%token	tOS
%token	tPASSNO
%token        tDIRECT
%token	tSEL
%token	<s> tSTR

%start list_of_hosts

%type <a> automount
%type <q> automount_tree
%type <e> ether_attr
%type <m> dir_tree_info
%type <d> filesystem fs_info_list
%type <h> host host_attr host_attr_list
%type <q> list_of_hosts list_of_filesystems list_of_mounts dir_tree
%type <f> localinfo_list
%type <s> opt_auto_opts

%%

list_of_hosts :
	  /* empty */
	  { $$ = new_que(); }

	| list_of_hosts host
	  { if ($2) ins_que((qelem *) $2, list_of_hosts->q_back);
	    $$ = $1; }

	| list_of_hosts automount
	  { if ($2) ins_que((qelem *) $2, list_of_automounts->q_back);
	    $$ = $1; }
	;

/*
 * A new host:
 *
 * host foo.domain
 */
host :
	  tHOST host_attr list_of_filesystems list_of_mounts
	  { $$ = $2; $$->h_disk_fs = $3; $$->h_mount = $4; }

	| error tHOST host_attr list_of_filesystems list_of_mounts
	  { $$ = $3; $$->h_disk_fs = $4; $$->h_mount = $5; }

	;

host_attr :
	  tSTR
	  { $$ = new_host(); set_host($$, HF_HOST, $1); }

	| '{' host_attr_list '}' tSTR
	  { $$ = $2; set_host($$, HF_HOST, $4); }

	;

host_attr_list :
	  /* empty */
	  { $$ = new_host(); }

	| host_attr_list tNETIF tSTR '{' ether_attr '}'
	  { if ($5) {
		$5->e_if = $3;
		$$ = $1; set_host($$, HF_ETHER, (char *) $5); }
	  }

	| host_attr_list tCONFIG tSTR
	  { $$ = $1; set_host($$, HF_CONFIG, $3); }

	| host_attr_list tARCH '=' tSTR
	  { $$ = $1; set_host($$, HF_ARCH, $4); }

	| host_attr_list tOS '=' tSTR
	  { $$ = $1; set_host($$, HF_OS, $4); }

	| host_attr_list tCLUSTER '=' tSTR
	  { $$ = $1; set_host($$, HF_CLUSTER, $4); }

	| host_attr_list error '=' tSTR
	  { yyerror("unknown host attribute"); }
	;

ether_attr :
	  /* empty */
	  { $$ = new_ether_if(); }

	| ether_attr tINADDR '=' tSTR
	  { $$ = $1; set_ether_if($$, EF_INADDR, $4); }
	| ether_attr tNETMASK '=' tSTR
	  { $$ = $1; set_ether_if($$, EF_NETMASK, $4); }
	| ether_attr tHWADDR '=' tSTR
	  { $$ = $1; set_ether_if($$, EF_HWADDR, $4); }
	;

/*
 * A new automount tree:
 *
 * automount /mountpoint { ... }
 */
automount :
	  tAUTOMOUNT opt_auto_opts automount_tree
	  { if ($3) {
		$$ = new_auto_tree($2, $3);
	    } else {
		$$ = 0;
	    }
	  }

	| tAUTOMOUNT error
	  { $$ = 0; }
	;

opt_auto_opts :
	  /* empty */
	  { $$ = xstrdup(""); }

	| tOPTS tSTR
	  { $$ = $2; }
	;

list_of_filesystems :
	  /* empty */
	  { $$ = 0; }

	| list_of_filesystems filesystem
	  { if ($2) {
		if ($1)
			$$ = $1;
		else
			$$ = new_que();
		ins_que(&$2->d_q, $$->q_back);
	    } else {
		$$ = $1;
	    }
	  }
	;

/*
 * A new filesystem:
 *
 * fs /dev/whatever { ... }
 */
filesystem :
	  tFS tSTR '{' fs_info_list '}'
	  { $4->d_dev = $2; $$ = $4; }

	| tFS error '}'
	  { $$ = (disk_fs *) NULL; }
	;

/*
 * Per-filesystem information:
 *
 * fstype - the type of the filesystem (4.2, nfs, swap, export)
 * opts - the mount options ("rw,grpid")
 * passno - fsck pass number
 * freq - dump frequency
 * dumpset - tape set for filesystem dumps
 * mount - where to mount this filesystem
 * log - log device
 */
fs_info_list :
	  /* empty */
	  { $$ = new_disk_fs(); }

	| fs_info_list tFSTYPE '=' tSTR
	  { $$ = $1; set_disk_fs($$, DF_FSTYPE, $4); }

	| fs_info_list tOPTS '=' tSTR
	  { $$ = $1; set_disk_fs($$, DF_OPTS, $4); }

	| fs_info_list tPASSNO '=' tSTR
	  { $$ = $1; set_disk_fs($$, DF_PASSNO, $4); }

	| fs_info_list tFREQ '=' tSTR
	  { $$ = $1; set_disk_fs($$, DF_FREQ, $4); }

	| fs_info_list tMOUNT dir_tree
	  { $$ = $1; set_disk_fs($$, DF_MOUNT, (char *) $3); }

	| fs_info_list tDUMPSET '=' tSTR
	  { $$ = $1; set_disk_fs($$, DF_DUMPSET, $4); }

	| fs_info_list tLOG '=' tSTR
	  { $$ = $1; set_disk_fs($$, DF_LOG, $4); }

	| fs_info_list error '=' tSTR
	  { yyerror("unknown filesystem attribute"); }
	;

/*
 * An automount tree:
 *
 * name = "volname"	name is a reference to volname
 * name -> "string"	name is a link to "string"
 * name nfsalias "string"  name is a link to "string", string parsed as NFS
 *			   pathname.
 * name { ... }		name is an automount tree
 */
automount_tree :
	  /* empty */
	  { $$ = 0; }

	| automount_tree tSTR opt_auto_opts '=' tSTR
	  { automount *a = new_automount($2);
	    a->a_volname = $5;
	    a->a_opts = $3;
	    if ($1)
		$$ = $1;
	    else
		$$ = new_que();
	    ins_que(&a->a_q, $$->q_back);
	  }
          | automount_tree tSTR opt_auto_opts tNFSEQ tSTR
            { automount *a = new_automount($2);
            a->a_hardwiredfs = $5;
            a->a_opts = $3;
            if ($1)
                $$ = $1;
            else
                $$ = new_que();
            ins_que(&a->a_q, $$->q_back);
          }

	| automount_tree tSTR tEQ tSTR
	  { automount *a = new_automount($2);
	    a->a_symlink = $4;
	    if ($1)
		$$ = $1;
	    else
		$$ = new_que();
	    ins_que(&a->a_q, $$->q_back);
	  }

	| automount_tree tSTR opt_auto_opts '{' automount_tree '}'
	  { automount *a = new_automount($2);
	    a->a_mount = $5;
	    a->a_opts = $3;
	    if ($1)
		$$ = $1;
	    else
		$$ = new_que();
	    ins_que(&a->a_q, $$->q_back);
	  }
	;

dir_tree :
	  /* empty */
	  { $$ = 0; }

	| dir_tree tSTR '{' dir_tree_info dir_tree '}'
	  { $4->m_mount = $5;
	    $4->m_name = $2;
	    if ($2[0] != '/' && $2[1] && strchr($2+1, '/'))
		yyerror("not allowed '/' in a directory name");
	    if ($1)
		$$ = $1;
	    else
		$$ = new_que();
	    ins_que(&$4->m_q, $$->q_back);
	  }
	;

dir_tree_info :
	  /* empty */
	  { $$ = new_mount(); }

	| dir_tree_info tEXPORTFS tSTR
	  { $$ = $1; set_mount($$, DM_EXPORTFS, $3); }

	| dir_tree_info tVOLNAME tSTR
	  { $$ = $1; set_mount($$, DM_VOLNAME, $3); }

	| dir_tree_info tSEL tSTR
	  { $$ = $1; set_mount($$, DM_SEL, $3); }

	| dir_tree_info error '=' tSTR
	  { yyerror("unknown directory attribute"); }
	;

/*
 * Additional mounts on a host
 *
 * mount "volname" ...
 */
list_of_mounts :
	  /* empty */
	  { $$ = 0; }

	| list_of_mounts tMOUNT tSTR localinfo_list
	  { set_fsmount($4, FM_VOLNAME, $3);
	    if ($1)
		$$ = $1;
	    else
		$$ = new_que();
	    ins_que(&$4->f_q, $$->q_back);
	    }
	;

/*
 * Mount info:
 *
 * from "hostname"	- obtain the object from the named host
 * as "string"		- where to mount, if different from the volname
 * opts "string"	- mount options
 * fstype "type"	- type of filesystem mount, if not nfs
 * direct             - mount entry, no need to create ad-hoc hosts file
 */
localinfo_list :
	  /* empty */
	  { $$ = new_fsmount(); }

        | localinfo_list tDIRECT
          { $$ = $1; set_fsmount($$, FM_DIRECT, ""); }

	| localinfo_list tAS tSTR
	  { $$ = $1; set_fsmount($$, FM_LOCALNAME, $3); }

	| localinfo_list tFROM tSTR
	  { $$ = $1; set_fsmount($$, FM_FROM, $3); }

	| localinfo_list tFSTYPE tSTR
	  { $$ = $1; set_fsmount($$, FM_FSTYPE, $3); }

	| localinfo_list tOPTS tSTR
	  { $$ = $1; set_fsmount($$, FM_OPTS, $3); }

	| localinfo_list error '=' tSTR
	  { yyerror("unknown mount attribute"); }
	;
