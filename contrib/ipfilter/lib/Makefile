#
# Copyright (C) 2012 by Darren Reed.
#
# See the IPFILTER.LICENCE file for details on licencing.
#
# $Id$
#
INCDEP=$(TOP)/ip_compat.h $(TOP)/ip_fil.h $(TOP)/ipf.h

LIBOBJS=$(DEST)/addicmp.o \
     $(DEST)/addipopt.o \
     $(DEST)/alist_free.o \
     $(DEST)/alist_new.o \
     $(DEST)/allocmbt.o \
     $(DEST)/assigndefined.o \
     $(DEST)/bcopywrap.o \
     $(DEST)/binprint.o \
     $(DEST)/buildopts.o \
     $(DEST)/checkrev.o \
     $(DEST)/connecttcp.o \
     $(DEST)/count6bits.o \
     $(DEST)/count4bits.o \
     $(DEST)/debug.o \
     $(DEST)/dupmbt.o \
     $(DEST)/familyname.o \
     $(DEST)/facpri.o \
     $(DEST)/fill6bits.o \
     $(DEST)/findword.o \
     $(DEST)/flags.o \
     $(DEST)/freembt.o \
     $(DEST)/ftov.o \
     $(DEST)/genmask.o \
     $(DEST)/gethost.o \
     $(DEST)/geticmptype.o \
     $(DEST)/getifname.o \
     $(DEST)/getnattype.o \
     $(DEST)/getport.o \
     $(DEST)/getportproto.o \
     $(DEST)/getproto.o \
     $(DEST)/getsumd.o \
     $(DEST)/hostname.o \
     $(DEST)/icmpcode.o \
     $(DEST)/icmptypename.o \
     $(DEST)/icmptypes.o \
     $(DEST)/initparse.o \
     $(DEST)/interror.o \
     $(DEST)/ionames.o \
     $(DEST)/ipf_dotuning.o \
     $(DEST)/ipf_perror.o \
     $(DEST)/ipft_hx.o \
     $(DEST)/ipft_pc.o \
     $(DEST)/ipft_tx.o \
     $(DEST)/ipoptsec.o \
     $(DEST)/kmem.o \
     $(DEST)/kmemcpywrap.o \
     $(DEST)/kvatoname.o \
     $(DEST)/load_file.o \
     $(DEST)/load_dstlist.o \
     $(DEST)/load_dstlistnode.o \
     $(DEST)/load_hash.o \
     $(DEST)/load_hashnode.o \
     $(DEST)/load_http.o \
     $(DEST)/load_pool.o \
     $(DEST)/load_poolnode.o \
     $(DEST)/load_url.o \
     $(DEST)/msgdsize.o \
     $(DEST)/mutex_emul.o \
     $(DEST)/nametokva.o \
     $(DEST)/nat_setgroupmap.o \
     $(DEST)/ntomask.o \
     $(DEST)/optname.o \
     $(DEST)/optprint.o \
     $(DEST)/optprintv6.o \
     $(DEST)/optvalue.o \
     $(DEST)/parsefields.o \
     $(DEST)/parseipfexpr.o \
     $(DEST)/parsewhoisline.o \
     $(DEST)/poolio.o \
     $(DEST)/portname.o \
     $(DEST)/print_toif.o \
     $(DEST)/printactiveaddr.o \
     $(DEST)/printactivenat.o \
     $(DEST)/printaddr.o \
     $(DEST)/printaps.o \
     $(DEST)/printbuf.o \
     $(DEST)/printdstlist.o \
     $(DEST)/printdstlistdata.o \
     $(DEST)/printdstlistnode.o \
     $(DEST)/printdstlistpolicy.o \
     $(DEST)/printdstl_live.o \
     $(DEST)/printfieldhdr.o \
     $(DEST)/printfr.o \
     $(DEST)/printfraginfo.o \
     $(DEST)/printhash.o \
     $(DEST)/printhashdata.o \
     $(DEST)/printhashnode.o \
     $(DEST)/printhash_live.o \
     $(DEST)/printhost.o \
     $(DEST)/printhostmap.o \
     $(DEST)/printhostmask.o \
     $(DEST)/printifname.o \
     $(DEST)/printip.o \
     $(DEST)/printipfexpr.o \
     $(DEST)/printlog.o \
     $(DEST)/printlookup.o \
     $(DEST)/printmask.o \
     $(DEST)/printnat.o \
     $(DEST)/printnataddr.o \
     $(DEST)/printnatfield.o \
     $(DEST)/printnatside.o \
     $(DEST)/printpool.o \
     $(DEST)/printpooldata.o \
     $(DEST)/printpoolfield.o \
     $(DEST)/printpoolnode.o \
     $(DEST)/printpool_live.o \
     $(DEST)/printproto.o \
     $(DEST)/printportcmp.o \
     $(DEST)/printpacket.o \
     $(DEST)/printpacket6.o \
     $(DEST)/printsbuf.o \
     $(DEST)/printstate.o \
     $(DEST)/printstatefields.o \
     $(DEST)/printtcpflags.o \
     $(DEST)/printtqtable.o \
     $(DEST)/printtunable.o \
     $(DEST)/printunit.o \
     $(DEST)/remove_hash.o \
     $(DEST)/remove_hashnode.o \
     $(DEST)/remove_pool.o \
     $(DEST)/remove_poolnode.o \
     $(DEST)/resetlexer.o \
     $(DEST)/rwlock_emul.o \
     $(DEST)/save_execute.o \
     $(DEST)/save_file.o \
     $(DEST)/save_nothing.o \
     $(DEST)/save_syslog.o \
     $(DEST)/save_v1trap.o \
     $(DEST)/save_v2trap.o \
     $(DEST)/tcpflags.o \
     $(DEST)/var.o \
     $(DEST)/verbose.o \
     $(DEST)/vtof.o \
     $(DEST)/v6ionames.o \
     $(DEST)/v6optvalue.o

$(DEST)/libipf.a: $(LIBOBJS)
	/bin/rm -f $@
	ar $(AROPTS) $@ $(LIBOBJS)
	$(RANLIB) $@

$(DEST)/addicmp.o: $(LIBSRC)/addicmp.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/addicmp.c -o $@
$(DEST)/addipopt.o: $(LIBSRC)/addipopt.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/addipopt.c -o $@
$(DEST)/alist_free.o: $(LIBSRC)/alist_free.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/alist_free.c -o $@
$(DEST)/alist_new.o: $(LIBSRC)/alist_new.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/alist_new.c -o $@
$(DEST)/allocmbt.o: $(LIBSRC)/allocmbt.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/allocmbt.c -o $@
$(DEST)/assigndefined.o: $(LIBSRC)/assigndefined.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/assigndefined.c -o $@
$(DEST)/bcopywrap.o: $(LIBSRC)/bcopywrap.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/bcopywrap.c -o $@
$(DEST)/binprint.o: $(LIBSRC)/binprint.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/binprint.c -o $@
$(DEST)/buildopts.o: $(LIBSRC)/buildopts.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/buildopts.c -o $@
$(DEST)/connecttcp.o: $(LIBSRC)/connecttcp.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/connecttcp.c -o $@
$(DEST)/count6bits.o: $(LIBSRC)/count6bits.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/count6bits.c -o $@
$(DEST)/checkrev.o: $(LIBSRC)/checkrev.c $(INCDEP) $(TOP)/ipl.h
	$(CC) $(CCARGS) -c $(LIBSRC)/checkrev.c -o $@
$(DEST)/count4bits.o: $(LIBSRC)/count4bits.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/count4bits.c -o $@
$(DEST)/debug.o: $(LIBSRC)/debug.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/debug.c -o $@
$(DEST)/dupmbt.o: $(LIBSRC)/dupmbt.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/dupmbt.c -o $@
$(DEST)/facpri.o: $(LIBSRC)/facpri.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/facpri.c -o $@
$(DEST)/familyname.o: $(LIBSRC)/familyname.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/familyname.c -o $@
$(DEST)/fill6bits.o: $(LIBSRC)/fill6bits.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/fill6bits.c -o $@
$(DEST)/findword.o: $(LIBSRC)/findword.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/findword.c -o $@
$(DEST)/flags.o: $(LIBSRC)/flags.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/flags.c -o $@
$(DEST)/freembt.o: $(LIBSRC)/freembt.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/freembt.c -o $@
$(DEST)/ftov.o: $(LIBSRC)/ftov.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ftov.c -o $@
$(DEST)/genmask.o: $(LIBSRC)/genmask.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/genmask.c -o $@
$(DEST)/gethost.o: $(LIBSRC)/gethost.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/gethost.c -o $@
$(DEST)/geticmptype.o: $(LIBSRC)/geticmptype.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/geticmptype.c -o $@
$(DEST)/getifname.o: $(LIBSRC)/getifname.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/getifname.c -o $@
$(DEST)/getnattype.o: $(LIBSRC)/getnattype.c $(INCDEP) $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/getnattype.c -o $@
$(DEST)/getport.o: $(LIBSRC)/getport.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/getport.c -o $@
$(DEST)/getportproto.o: $(LIBSRC)/getportproto.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/getportproto.c -o $@
$(DEST)/getproto.o: $(LIBSRC)/getproto.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/getproto.c -o $@
$(DEST)/getsumd.o: $(LIBSRC)/getsumd.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/getsumd.c -o $@
$(DEST)/hostname.o: $(LIBSRC)/hostname.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/hostname.c -o $@
$(DEST)/icmpcode.o: $(LIBSRC)/icmpcode.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/icmpcode.c -o $@
$(DEST)/icmptypename.o: $(LIBSRC)/icmptypename.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/icmptypename.c -o $@
$(DEST)/icmptypes.o: $(LIBSRC)/icmptypes.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/icmptypes.c -o $@
$(DEST)/interror.o: $(LIBSRC)/interror.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/interror.c -o $@
$(DEST)/ipoptsec.o: $(LIBSRC)/ipoptsec.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ipoptsec.c -o $@
$(DEST)/initparse.o: $(LIBSRC)/initparse.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/initparse.c -o $@
$(DEST)/ionames.o: $(LIBSRC)/ionames.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ionames.c -o $@
$(DEST)/ipf_dotuning.o: $(LIBSRC)/ipf_dotuning.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ipf_dotuning.c -o $@
$(DEST)/ipf_perror.o: $(LIBSRC)/ipf_perror.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ipf_perror.c -o $@
$(DEST)/ipft_hx.o: $(LIBSRC)/ipft_hx.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ipft_hx.c -o $@
$(DEST)/ipft_pc.o: $(LIBSRC)/ipft_pc.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ipft_pc.c -o $@
$(DEST)/ipft_tx.o: $(LIBSRC)/ipft_tx.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/ipft_tx.c -o $@
$(DEST)/kmem.o: $(LIBSRC)/kmem.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/kmem.c -o $@
$(DEST)/kmemcpywrap.o: $(LIBSRC)/kmemcpywrap.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/kmemcpywrap.c -o $@
$(DEST)/kvatoname.o: $(LIBSRC)/kvatoname.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/kvatoname.c -o $@
$(DEST)/load_file.o: $(LIBSRC)/load_file.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/load_file.c -o $@
$(DEST)/load_dstlist.o: $(LIBSRC)/load_dstlist.c $(INCDEP) $(TOP)/ip_dstlist.h
	$(CC) $(CCARGS) -c $(LIBSRC)/load_dstlist.c -o $@
$(DEST)/load_dstlistnode.o: $(LIBSRC)/load_dstlistnode.c $(INCDEP) \
    $(TOP)/ip_dstlist.h
	$(CC) $(CCARGS) -c $(LIBSRC)/load_dstlistnode.c -o $@
$(DEST)/load_hash.o: $(LIBSRC)/load_hash.c $(INCDEP) $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/load_hash.c -o $@
$(DEST)/load_hashnode.o: $(LIBSRC)/load_hashnode.c $(INCDEP) $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/load_hashnode.c -o $@
$(DEST)/load_http.o: $(LIBSRC)/load_http.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/load_http.c -o $@
$(DEST)/load_pool.o: $(LIBSRC)/load_pool.c $(INCDEP) $(TOP)/ip_pool.h
	$(CC) $(CCARGS) -c $(LIBSRC)/load_pool.c -o $@
$(DEST)/load_poolnode.o: $(LIBSRC)/load_poolnode.c $(INCDEP) $(TOP)/ip_pool.h
	$(CC) $(CCARGS) -c $(LIBSRC)/load_poolnode.c -o $@
$(DEST)/load_url.o: $(LIBSRC)/load_url.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/load_url.c -o $@
$(DEST)/msgdsize.o: $(LIBSRC)/msgdsize.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/msgdsize.c -o $@
$(DEST)/mutex_emul.o: $(LIBSRC)/mutex_emul.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/mutex_emul.c -o $@
$(DEST)/nametokva.o: $(LIBSRC)/nametokva.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/nametokva.c -o $@
$(DEST)/nat_setgroupmap.o: $(LIBSRC)/nat_setgroupmap.c $(TOP)/ip_compat.h \
    $(TOP)/ipf.h $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/nat_setgroupmap.c -o $@
$(DEST)/ntomask.o: $(LIBSRC)/ntomask.c $(TOP)/ip_compat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/ntomask.c -o $@
$(DEST)/optname.o: $(LIBSRC)/optname.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/optname.c -o $@
$(DEST)/optprint.o: $(LIBSRC)/optprint.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/optprint.c -o $@
$(DEST)/optprintv6.o: $(LIBSRC)/optprintv6.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/optprintv6.c -o $@
$(DEST)/optvalue.o: $(LIBSRC)/optvalue.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/optvalue.c -o $@
$(DEST)/parsefields.o: $(LIBSRC)/parsefields.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/parsefields.c -o $@
$(DEST)/parseipfexpr.o: $(LIBSRC)/parseipfexpr.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/parseipfexpr.c -o $@
$(DEST)/parsewhoisline.o: $(LIBSRC)/parsewhoisline.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/parsewhoisline.c -o $@
$(DEST)/poolio.o: $(LIBSRC)/poolio.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/poolio.c -o $@
$(DEST)/portname.o: $(LIBSRC)/portname.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/portname.c -o $@
$(DEST)/print_toif.o: $(LIBSRC)/print_toif.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/print_toif.c -o $@
$(DEST)/printactiveaddr.o: $(LIBSRC)/printactiveaddr.c $(INCDEP) $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printactiveaddr.c -o $@
$(DEST)/printactivenat.o: $(LIBSRC)/printactivenat.c $(INCDEP) $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printactivenat.c -o $@
$(DEST)/printaddr.o: $(LIBSRC)/printaddr.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printaddr.c -o $@
$(DEST)/printaps.o: $(LIBSRC)/printaps.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printaps.c -o $@
$(DEST)/printbuf.o: $(LIBSRC)/printbuf.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printbuf.c -o $@
$(DEST)/printdstlist.o: $(LIBSRC)/printdstlist.c $(INCDEP) $(TOP)/ip_dstlist.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printdstlist.c -o $@
$(DEST)/printdstlistdata.o: $(LIBSRC)/printdstlistdata.c $(INCDEP) \
    $(TOP)/ip_dstlist.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printdstlistdata.c -o $@
$(DEST)/printdstlistnode.o: $(LIBSRC)/printdstlistnode.c $(INCDEP) \
    $(TOP)/ip_dstlist.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printdstlistnode.c -o $@
$(DEST)/printdstlistpolicy.o: $(LIBSRC)/printdstlistpolicy.c $(INCDEP) \
    $(TOP)/ip_dstlist.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printdstlistpolicy.c -o $@
$(DEST)/printfieldhdr.o: $(LIBSRC)/printfieldhdr.c $(TOP)/ip_fil.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printfieldhdr.c -o $@
$(DEST)/printfr.o: $(LIBSRC)/printfr.c $(TOP)/ip_fil.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printfr.c -o $@
$(DEST)/printfraginfo.o: $(LIBSRC)/printfraginfo.c $(TOP)/ip_fil.h \
    $(TOP)/ip_frag.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printfraginfo.c -o $@
$(DEST)/printhash.o: $(LIBSRC)/printhash.c $(TOP)/ip_fil.h $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printhash.c -o $@
$(DEST)/printhashdata.o: $(LIBSRC)/printhashdata.c $(TOP)/ip_fil.h \
    $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printhashdata.c -o $@
$(DEST)/printhashnode.o: $(LIBSRC)/printhashnode.c $(TOP)/ip_fil.h \
    $(TOP)/ip_htable.h $(TOP)/ip_lookup.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printhashnode.c -o $@
$(DEST)/printhash_live.o: $(LIBSRC)/printhash_live.c $(TOP)/ip_fil.h \
    $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printhash_live.c -o $@
$(DEST)/printdstl_live.o: $(LIBSRC)/printdstl_live.c $(TOP)/ip_fil.h \
    $(TOP)/ip_dstlist.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printdstl_live.c -o $@
$(DEST)/printip.o: $(LIBSRC)/printip.c $(TOP)/ip_fil.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printip.c -o $@
$(DEST)/printipfexpr.o: $(LIBSRC)/printipfexpr.c $(TOP)/ip_fil.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printipfexpr.c -o $@
$(DEST)/printlookup.o: $(LIBSRC)/printlookup.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printlookup.c -o $@
$(DEST)/printnataddr.o: $(LIBSRC)/printnataddr.c $(INCDEP) $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printnataddr.c -o $@
$(DEST)/printnatside.o: $(LIBSRC)/printnatside.c $(INCDEP) $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printnatside.c -o $@
$(DEST)/printpool.o: $(LIBSRC)/printpool.c $(TOP)/ip_fil.h $(TOP)/ip_pool.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printpool.c -o $@
$(DEST)/printpooldata.o: $(LIBSRC)/printpooldata.c $(TOP)/ip_fil.h \
    $(TOP)/ip_pool.h $(TOP)/ip_lookup.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printpooldata.c -o $@
$(DEST)/printpoolfield.o: $(LIBSRC)/printpoolfield.c $(TOP)/ip_fil.h \
    $(TOP)/ip_pool.h $(TOP)/ip_lookup.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printpoolfield.c -o $@
$(DEST)/printpoolnode.o: $(LIBSRC)/printpoolnode.c $(TOP)/ip_fil.h \
    $(TOP)/ip_pool.h $(TOP)/ip_lookup.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printpoolnode.c -o $@
$(DEST)/printpool_live.o: $(LIBSRC)/printpool_live.c $(TOP)/ip_fil.h \
    $(TOP)/ip_pool.h $(TOP)/ip_lookup.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printpool_live.c -o $@
$(DEST)/printproto.o: $(LIBSRC)/printproto.c $(TOP)/ip_fil.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printproto.c -o $@
$(DEST)/printhost.o: $(LIBSRC)/printhost.c $(TOP)/ip_fil.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printhost.c -o $@
$(DEST)/printhostmap.o: $(LIBSRC)/printhostmap.c $(TOP)/ip_fil.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printhostmap.c -o $@
$(DEST)/printifname.o: $(LIBSRC)/printifname.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printifname.c -o $@
$(DEST)/printmask.o: $(LIBSRC)/printmask.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printmask.c -o $@
$(DEST)/printnat.o: $(LIBSRC)/printnat.c $(INCDEP) $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printnat.c -o $@
$(DEST)/printnatfield.o: $(LIBSRC)/printnatfield.c $(INCDEP) $(TOP)/ip_nat.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printnatfield.c -o $@
$(DEST)/printhostmask.o: $(LIBSRC)/printhostmask.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printhostmask.c -o $@
$(DEST)/printlog.o: $(LIBSRC)/printlog.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printlog.c -o $@
$(DEST)/printpacket.o: $(LIBSRC)/printpacket.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printpacket.c -o $@
$(DEST)/printpacket6.o: $(LIBSRC)/printpacket6.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printpacket6.c -o $@
$(DEST)/printportcmp.o: $(LIBSRC)/printportcmp.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printportcmp.c -o $@
$(DEST)/printsbuf.o: $(LIBSRC)/printsbuf.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printsbuf.c -o $@
$(DEST)/printstate.o: $(LIBSRC)/printstate.c $(INCDEP) $(TOP)/ip_state.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printstate.c -o $@
$(DEST)/printstatefields.o: $(LIBSRC)/printstatefields.c $(INCDEP) $(TOP)/ip_state.h
	$(CC) $(CCARGS) -c $(LIBSRC)/printstatefields.c -o $@
$(DEST)/printtcpflags.o: $(LIBSRC)/printtcpflags.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printtcpflags.c -o $@
$(DEST)/printtqtable.o: $(LIBSRC)/printtqtable.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printtqtable.c -o $@
$(DEST)/printtunable.o: $(LIBSRC)/printtunable.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printtunable.c -o $@
$(DEST)/printunit.o: $(LIBSRC)/printunit.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/printunit.c -o $@
$(DEST)/remove_hash.o: $(LIBSRC)/remove_hash.c $(INCDEP) \
    $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/remove_hash.c -o $@
$(DEST)/remove_hashnode.o: $(LIBSRC)/remove_hashnode.c $(INCDEP) \
    $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/remove_hashnode.c -o $@
$(DEST)/remove_pool.o: $(LIBSRC)/remove_pool.c $(INCDEP) \
    $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/remove_pool.c -o $@
$(DEST)/remove_poolnode.o: $(LIBSRC)/remove_poolnode.c $(INCDEP) \
    $(TOP)/ip_htable.h
	$(CC) $(CCARGS) -c $(LIBSRC)/remove_poolnode.c -o $@
$(DEST)/resetlexer.o: $(LIBSRC)/resetlexer.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/resetlexer.c -o $@
$(DEST)/rwlock_emul.o: $(LIBSRC)/rwlock_emul.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/rwlock_emul.c -o $@
$(DEST)/tcpflags.o: $(LIBSRC)/tcpflags.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/tcpflags.c -o $@
$(DEST)/tcp_flags.o: $(LIBSRC)/tcp_flags.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/tcp_flags.c -o $@
$(DEST)/var.o: $(LIBSRC)/var.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/var.c -o $@
$(DEST)/verbose.o: $(LIBSRC)/verbose.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/verbose.c -o $@
$(DEST)/save_execute.o: $(LIBSRC)/save_execute.c $(TOP)/ipl.h
	$(CC) $(CCARGS) -c $(LIBSRC)/save_execute.c -o $@
$(DEST)/save_file.o: $(LIBSRC)/save_file.c $(TOP)/ipl.h
	$(CC) $(CCARGS) -c $(LIBSRC)/save_file.c -o $@
$(DEST)/save_nothing.o: $(LIBSRC)/save_nothing.c $(TOP)/ipl.h
	$(CC) $(CCARGS) -c $(LIBSRC)/save_nothing.c -o $@
$(DEST)/save_syslog.o: $(LIBSRC)/save_syslog.c $(TOP)/ipl.h
	$(CC) $(CCARGS) -c $(LIBSRC)/save_syslog.c -o $@
$(DEST)/vtof.o: $(LIBSRC)/vtof.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/vtof.c -o $@
$(DEST)/save_v1trap.o: $(LIBSRC)/save_v1trap.c $(TOP)/ipl.h
	$(CC) $(CCARGS) -c $(LIBSRC)/save_v1trap.c -o $@
$(DEST)/save_v2trap.o: $(LIBSRC)/save_v2trap.c $(TOP)/ipl.h
	$(CC) $(CCARGS) -c $(LIBSRC)/save_v2trap.c -o $@
$(DEST)/v6ionames.o: $(LIBSRC)/v6ionames.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/v6ionames.c -o $@
$(DEST)/v6optvalue.o: $(LIBSRC)/v6optvalue.c $(INCDEP)
	$(CC) $(CCARGS) -c $(LIBSRC)/v6optvalue.c -o $@

clean-lib:
	/bin/rm -f ${LIBOBJS} ${LIB}
