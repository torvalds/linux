###
# This makefile is used to generate the kernel documentation,
# primarily based on in-line comments in various source files.
# See Documentation/kernel-doc-nano-HOWTO.txt for instruction in how
# to document the SRC - and how to read it.
# To add a new book the only step required is to add the book to the
# list of DOCBOOKS.

TMPMEDIA=.tmpmedia

DOCBOOKS := z8530book.xml mcabook.xml device-drivers.xml \
	    kernel-hacking.xml kernel-locking.xml deviceiobook.xml \
	    writing_usb_driver.xml networking.xml \
	    kernel-api.xml filesystems.xml lsm.xml usb.xml kgdb.xml \
	    gadget.xml libata.xml mtdnand.xml librs.xml rapidio.xml \
	    genericirq.xml s390-drivers.xml uio-howto.xml scsi.xml \
	    80211.xml debugobjects.xml sh.xml regulator.xml \
	    alsa-driver-api.xml writing-an-alsa-driver.xml \
	    tracepoint.xml $(TMPMEDIA)/media.xml drm.xml

###
# The build process is as follows (targets):
#              (xmldocs) [by docproc]
# file.tmpl --> file.xml +--> file.ps   (psdocs)   [by db2ps or xmlto]
#                        +--> file.pdf  (pdfdocs)  [by db2pdf or xmlto]
#                        +--> DIR=file  (htmldocs) [by xmlto]
#                        +--> man/      (mandocs)  [by xmlto]


# for PDF and PS output you can choose between xmlto and docbook-utils tools
PDF_METHOD	= $(prefer-db2x)
PS_METHOD	= $(prefer-db2x)


###
# The targets that may be used.
PHONY += xmldocs sgmldocs psdocs pdfdocs htmldocs mandocs installmandocs cleandocs mediaprep

BOOKS := $(addprefix $(obj)/,$(DOCBOOKS))
xmldocs: $(BOOKS)
sgmldocs: xmldocs

PS := $(patsubst %.xml, %.ps, $(BOOKS))
psdocs: $(PS)

PDF := $(patsubst %.xml, %.pdf, $(BOOKS))
pdfdocs: $(PDF)

HTML := $(sort $(patsubst %.xml, %.html, $(BOOKS)))
htmldocs: $(HTML)
	$(call build_main_index)
	$(call build_images)

MAN := $(patsubst %.xml, %.9, $(BOOKS))
mandocs: $(MAN)

installmandocs: mandocs
	mkdir -p /usr/local/man/man9/
	install Documentation/DocBook/man/*.9.gz /usr/local/man/man9/

###
#External programs used
KERNELDOC = $(srctree)/scripts/kernel-doc
DOCPROC   = $(objtree)/scripts/docproc

XMLTOFLAGS = -m $(srctree)/Documentation/DocBook/stylesheet.xsl
XMLTOFLAGS += --skip-validation

###
# DOCPROC is used for two purposes:
# 1) To generate a dependency list for a .tmpl file
# 2) To preprocess a .tmpl file and call kernel-doc with
#     appropriate parameters.
# The following rules are used to generate the .xml documentation
# required to generate the final targets. (ps, pdf, html).
quiet_cmd_docproc = DOCPROC $@
      cmd_docproc = SRCTREE=$(srctree)/ $(DOCPROC) doc $< >$@
define rule_docproc
	set -e;								\
        $(if $($(quiet)cmd_$(1)),echo '  $($(quiet)cmd_$(1))';) 	\
        $(cmd_$(1)); 							\
        ( 								\
          echo 'cmd_$@ := $(cmd_$(1))'; 				\
          echo $@: `SRCTREE=$(srctree) $(DOCPROC) depend $<`; 		\
        ) > $(dir $@).$(notdir $@).cmd
endef

%.xml: %.tmpl FORCE
	$(call if_changed_rule,docproc)

###
#Read in all saved dependency files
cmd_files := $(wildcard $(foreach f,$(BOOKS),$(dir $(f)).$(notdir $(f)).cmd))

ifneq ($(cmd_files),)
  include $(cmd_files)
endif

###
# Changes in kernel-doc force a rebuild of all documentation
$(BOOKS): $(KERNELDOC)

# Tell kbuild to always build the programs
always := $(hostprogs-y)

notfoundtemplate = echo "*** You have to install docbook-utils or xmlto ***"; \
		   exit 1
db2xtemplate = db2TYPE -o $(dir $@) $<
xmltotemplate = xmlto TYPE $(XMLTOFLAGS) -o $(dir $@) $<

# determine which methods are available
ifeq ($(shell which db2ps >/dev/null 2>&1 && echo found),found)
	use-db2x = db2x
	prefer-db2x = db2x
else
	use-db2x = notfound
	prefer-db2x = $(use-xmlto)
endif
ifeq ($(shell which xmlto >/dev/null 2>&1 && echo found),found)
	use-xmlto = xmlto
	prefer-xmlto = xmlto
else
	use-xmlto = notfound
	prefer-xmlto = $(use-db2x)
endif

# the commands, generated from the chosen template
quiet_cmd_db2ps = PS      $@
      cmd_db2ps = $(subst TYPE,ps, $($(PS_METHOD)template))
%.ps : %.xml
	$(call cmd,db2ps)

quiet_cmd_db2pdf = PDF     $@
      cmd_db2pdf = $(subst TYPE,pdf, $($(PDF_METHOD)template))
%.pdf : %.xml
	$(call cmd,db2pdf)


index = index.html
main_idx = Documentation/DocBook/$(index)
build_main_index = rm -rf $(main_idx); \
		   echo '<h1>Linux Kernel HTML Documentation</h1>' >> $(main_idx) && \
		   echo '<h2>Kernel Version: $(KERNELVERSION)</h2>' >> $(main_idx) && \
		   cat $(HTML) >> $(main_idx)

quiet_cmd_db2html = HTML    $@
      cmd_db2html = xmlto xhtml $(XMLTOFLAGS) -o $(patsubst %.html,%,$@) $< && \
		echo '<a HREF="$(patsubst %.html,%,$(notdir $@))/index.html"> \
        $(patsubst %.html,%,$(notdir $@))</a><p>' > $@

%.html:	%.xml
	@(which xmlto > /dev/null 2>&1) || \
	 (echo "*** You need to install xmlto ***"; \
	  exit 1)
	@rm -rf $@ $(patsubst %.html,%,$@)
	$(call cmd,db2html)
	@if [ ! -z "$(PNG-$(basename $(notdir $@)))" ]; then \
            cp $(PNG-$(basename $(notdir $@))) $(patsubst %.html,%,$@); fi

quiet_cmd_db2man = MAN     $@
      cmd_db2man = if grep -q refentry $<; then xmlto man $(XMLTOFLAGS) -o $(obj)/man $< ; gzip -f $(obj)/man/*.9; fi
%.9 : %.xml
	@(which xmlto > /dev/null 2>&1) || \
	 (echo "*** You need to install xmlto ***"; \
	  exit 1)
	$(Q)mkdir -p $(obj)/man
	$(call cmd,db2man)
	@touch $@

###
# Rules to generate postscripts and PNG images from .fig format files
quiet_cmd_fig2eps = FIG2EPS $@
      cmd_fig2eps = fig2dev -Leps $< $@

%.eps: %.fig
	@(which fig2dev > /dev/null 2>&1) || \
	 (echo "*** You need to install transfig ***"; \
	  exit 1)
	$(call cmd,fig2eps)

quiet_cmd_fig2png = FIG2PNG $@
      cmd_fig2png = fig2dev -Lpng $< $@

%.png: %.fig
	@(which fig2dev > /dev/null 2>&1) || \
	 (echo "*** You need to install transfig ***"; \
	  exit 1)
	$(call cmd,fig2png)

###
# Rule to convert a .c file to inline XML documentation
       gen_xml = :
 quiet_gen_xml = echo '  GEN     $@'
silent_gen_xml = :
%.xml: %.c
	@$($(quiet)gen_xml)
	@(                            \
	   echo "<programlisting>";   \
	   expand --tabs=8 < $< |     \
	   sed -e "s/&/\\&amp;/g"     \
	       -e "s/</\\&lt;/g"      \
	       -e "s/>/\\&gt;/g";     \
	   echo "</programlisting>")  > $@

###
# Help targets as used by the top-level makefile
dochelp:
	@echo  ' Linux kernel internal documentation in different formats:'
	@echo  '  htmldocs        - HTML'
	@echo  '  pdfdocs         - PDF'
	@echo  '  psdocs          - Postscript'
	@echo  '  xmldocs         - XML DocBook'
	@echo  '  mandocs         - man pages'
	@echo  '  installmandocs  - install man pages generated by mandocs'
	@echo  '  cleandocs       - clean all generated DocBook files'

###
# Temporary files left by various tools
clean-files := $(DOCBOOKS) \
	$(patsubst %.xml, %.dvi,  $(DOCBOOKS)) \
	$(patsubst %.xml, %.aux,  $(DOCBOOKS)) \
	$(patsubst %.xml, %.tex,  $(DOCBOOKS)) \
	$(patsubst %.xml, %.log,  $(DOCBOOKS)) \
	$(patsubst %.xml, %.out,  $(DOCBOOKS)) \
	$(patsubst %.xml, %.ps,   $(DOCBOOKS)) \
	$(patsubst %.xml, %.pdf,  $(DOCBOOKS)) \
	$(patsubst %.xml, %.html, $(DOCBOOKS)) \
	$(patsubst %.xml, %.9,    $(DOCBOOKS)) \
	$(index)

clean-dirs := $(patsubst %.xml,%,$(DOCBOOKS)) man $(MEDIA_DIR)

cleandocs:
	$(Q)rm -f $(call objectify, $(clean-files))
	$(Q)rm -rf $(call objectify, $(clean-dirs))

# Declare the contents of the .PHONY variable as phony.  We keep that
# information in a variable se we can use it in if_changed and friends.

.PHONY: $(PHONY)


#
# Media build rules - Auto-generates media contents/indexes and *.h xml's
#

SHELL=/bin/bash

MEDIA_DIR=$(objtree)/Documentation/DocBook/$(TMPMEDIA)

V4L_SGMLS = \
	$(shell ls $(srctree)/Documentation/DocBook/v4l/*.xml|perl -ne 'print "$$1 " if (m,.*/(.*)\n,)') \
	capture.c.xml \
	keytable.c.xml \
	v4l2grab.c.xml

DVB_SGMLS = \
	$(shell ls $(srctree)/Documentation/DocBook/dvb/*.xml|perl -ne 'print "$$1 " if (m,.*/(.*)\n,)')

MEDIA_TEMP =  media-entities.tmpl \
	      media-indices.tmpl \
	      videodev2.h.xml \
	      frontend.h.xml

MEDIA_SGMLS =  $(addprefix ./,$(V4L_SGMLS)) $(addprefix ./,$(DVB_SGMLS)) $(addprefix ./,$(MEDIA_TEMP))

MEDIA_TEMP_OBJ := $(addprefix $(MEDIA_DIR)/,$(MEDIA_TEMP))

FUNCS = \
	close \
	ioctl \
	mmap \
	munmap \
	open \
	poll \
	read \
	select \
	write \

IOCTLS = \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/linux/videodev2.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/linux/media.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/linux/v4l2-subdev.h) \
	VIDIOC_SUBDEV_G_FRAME_INTERVAL \
	VIDIOC_SUBDEV_S_FRAME_INTERVAL \
	VIDIOC_SUBDEV_ENUM_MBUS_CODE \
	VIDIOC_SUBDEV_ENUM_FRAME_SIZE \
	VIDIOC_SUBDEV_ENUM_FRAME_INTERVAL \

TYPES = \
	$(shell perl -ne 'print "$$1 " if /^typedef\s+[^\s]+\s+([^\s]+)\;/' $(srctree)/include/linux/videodev2.h)

ENUMS = \
	$(shell perl -ne 'print "$$1 " if /^enum\s+([^\s]+)\s+/' $(srctree)/include/linux/videodev2.h) \
	$(shell perl -ne 'print "$$1 " if /^enum\s+([^\s]+)\s+/' $(srctree)/include/linux/media.h) \
	$(shell perl -ne 'print "$$1 " if /^enum\s+([^\s]+)\s+/' $(srctree)/include/linux/v4l2-mediabus.h) \
	$(shell perl -ne 'print "$$1 " if /^enum\s+([^\s]+)\s+/' $(srctree)/include/linux/v4l2-subdev.h)

STRUCTS = \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/linux/videodev2.h) \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/linux/media.h) \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/linux/v4l2-subdev.h) \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/linux/v4l2-mediabus.h)

ERRORS = \
	EACCES \
	EAGAIN \
	EBADF \
	EBUSY \
	EFAULT \
	EIO \
	EINTR \
	EINVAL \
	ENFILE \
	ENOMEM \
	ENOSPC \
	ENOTTY \
	ENXIO \
	EMFILE \
	EPERM \
	ERANGE \
	EPIPE \

ESCAPE = \
	-e "s/&/\\&amp;/g" \
	-e "s/</\\&lt;/g" \
	-e "s/>/\\&gt;/g"

FILENAME = \
	-e s,"^[^\/]*/",, \
	-e s/"\\.xml"// \
	-e s/"\\.tmpl"// \
	-e s/\\\./-/g \
	-e s/"^func-"// \
	-e s/"^pixfmt-"// \
	-e s/"^vidioc-"//

# Generate references to these structs in videodev2.h.xml.
DOCUMENTED = \
	-e "s/\(enum *\)v4l2_mpeg_cx2341x_video_\([a-z]*_spatial_filter_type\)/\1<link linkend=\"\2\">v4l2_mpeg_cx2341x_video_\2<\/link>/g" \
	-e "s/\(\(enum\|struct\) *\)\(v4l2_[a-zA-Z0-9_]*\)/\1<link linkend=\"\3\">\3<\/link>/g" \
	-e "s/\(V4L2_PIX_FMT_[A-Z0-9_]\+\) /<link linkend=\"\1\">\1<\/link> /g" \
	-e ":a;s/\(linkend=\".*\)_\(.*\">\)/\1-\2/;ta" \
	-e "s/v4l2\-mpeg\-vbi\-ITV0/v4l2-mpeg-vbi-itv0-1/g"

DVB_DOCUMENTED = \
	-e "s,\(define \)\([A-Z0-9_]\+\)\(\s\+_IO\),\1\<link linkend=\"\2\">\2\<\/link\>\3,g" \
	-e "s/\(linkend\=\"\)FE_SET_PROPERTY/\1FE_GET_PROPERTY/g"


#
# Media targets and dependencies
#

$(obj)/$(TMPMEDIA)/media.xml: $(obj)/media.tmpl $(MEDIA_TEMP_OBJ) FORCE
	$(call if_changed_rule,docproc)

$(MEDIA_DIR)/v4l2.xml:
	@$($(quiet)gen_xml)
	@(mkdir -p $(MEDIA_DIR))
	@(cp $(srctree)/Documentation/DocBook/dvb/*.png $(srctree)/Documentation/DocBook/v4l/*.gif $(MEDIA_DIR)/)
	@(ln -sf $(srctree)/Documentation/DocBook/v4l/*xml $(MEDIA_DIR)/)
	@(ln -sf $(srctree)/Documentation/DocBook/dvb/*xml $(MEDIA_DIR)/)

$(MEDIA_DIR)/videodev2.h.xml: $(srctree)/include/linux/videodev2.h $(MEDIA_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DOCUMENTED) |		\
	  sed 's/i\.e\./&ie;/') >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_DIR)/frontend.h.xml: $(srctree)/include/linux/dvb/frontend.h $(MEDIA_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DVB_DOCUMENTED) |	\
	  sed 's/i\.e\./&ie;/') >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_DIR)/media-entities.tmpl: $(MEDIA_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(								\
	echo "<!-- Generated file! Do not edit. -->") >$@
	@(								\
	echo -e "\n<!-- Functions -->") >>$@
	@(								\
	for ident in $(FUNCS) ; do					\
	  entity=`echo $$ident | tr _ -` ;				\
	  echo "<!ENTITY func-$$entity \"<link"				\
	    "linkend='func-$$entity'><function>$$ident()</function></link>\">" \
	  >>$@ ;							\
	done)
	@(								\
	echo -e "\n<!-- Ioctls -->") >>$@
	@(								\
	for ident in $(IOCTLS) ; do					\
	  entity=`echo $$ident | tr _ -` ;				\
	  id=`grep "<refname>$$ident" $(MEDIA_DIR)/vidioc-*.xml | sed -r s,"^.*/(.*).xml.*","\1",` ; \
	  echo "<!ENTITY $$entity \"<link"				\
	    "linkend='$$id'><constant>$$ident</constant></link>\">"	\
	  >>$@ ;							\
	done)
	@(								\
	echo -e "\n<!-- Types -->") >>$@
	@(								\
	for ident in $(TYPES) ; do					\
	  entity=`echo $$ident | tr _ -` ;				\
	  echo "<!ENTITY $$entity \"<link"				\
	    "linkend='$$entity'>$$ident</link>\">" >>$@ ;		\
	done)
	@(								\
	echo -e "\n<!-- Enums -->") >>$@
	@(								\
	for ident in $(ENUMS) ; do					\
	  entity=`echo $$ident | sed -e "s/v4l2_mpeg_cx2341x_video_\([a-z]*_spatial_filter_type\)/\1/" | tr _ -` ; \
	  echo "<!ENTITY $$entity \"enum&nbsp;<link"			\
	    "linkend='$$entity'>$$ident</link>\">" >>$@ ;		\
	done)
	@(								\
	echo -e "\n<!-- Structures -->") >>$@
	@(								\
	for ident in $(STRUCTS) ; do					\
	  entity=`echo $$ident | tr _ - | sed s/v4l2-mpeg-vbi-ITV0/v4l2-mpeg-vbi-itv0-1/g` ; \
	  echo "<!ENTITY $$entity \"struct&nbsp;<link"			\
	    "linkend='$$entity'>$$ident</link>\">" >>$@ ;		\
	done)
	@(								\
	echo -e "\n<!-- Error Codes -->") >>$@
	@(								\
	for ident in $(ERRORS) ; do					\
	  echo "<!ENTITY $$ident \"<errorcode>$$ident</errorcode>"	\
	    "error code\">" >>$@ ;					\
	done)
	@(								\
	echo -e "\n<!-- Subsections -->") >>$@
	@(								\
	for file in $(MEDIA_SGMLS) ; do					\
	  entity=`echo "$$file" | sed $(FILENAME) -e s/"^([^-]*)"/sub\1/` ; \
	  if ! echo "$$file" |						\
	    grep -q -E -e '^(func|vidioc|pixfmt)-' ; then		\
	    echo "<!ENTITY sub-$$entity SYSTEM \"$$file\">" >>$@ ;	\
	  fi ;								\
	done)
	@(								\
	echo -e "\n<!-- Function Reference -->") >>$@
	@(								\
	for file in $(MEDIA_SGMLS) ; do					\
	  if echo "$$file" |						\
	    grep -q -E -e '(func|vidioc|pixfmt)-' ; then		\
	    entity=`echo "$$file" |sed $(FILENAME)` ;			\
	    echo "<!ENTITY $$entity SYSTEM \"$$file\">" >>$@ ;	\
	  fi ;								\
	done)

# Jade can auto-generate a list-of-tables, which includes all structs,
# but we only want data types, all types, and sorted please.
$(MEDIA_DIR)/media-indices.tmpl: $(MEDIA_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(								\
	echo "<!-- Generated file! Do not edit. -->") >$@
	@(								\
	echo -e "\n<index><title>List of Types</title>") >>$@
	@(								\
	for ident in $(TYPES) ; do					\
	  id=`echo $$ident | tr _ -` ;					\
	  echo "<indexentry><primaryie><link"				\
	    "linkend='$$id'>$$ident</link></primaryie></indexentry>" >>$@ ; \
	done)
	@(								\
	for ident in $(ENUMS) ; do					\
	  id=`echo $$ident | sed -e "s/v4l2_mpeg_cx2341x_video_\([a-z]*_spatial_filter_type\)/\1/" | tr _ -`; \
	  echo "<indexentry><primaryie>enum&nbsp;<link"			\
	    "linkend='$$id'>$$ident</link></primaryie></indexentry>" >>$@ ; \
	done)
	@(								\
	for ident in $(STRUCTS) ; do					\
	  id=`echo $$ident | tr _ - | sed s/v4l2-mpeg-vbi-ITV0/v4l2-mpeg-vbi-itv0-1/g` ; \
	  echo "<indexentry><primaryie>struct&nbsp;<link"		\
	    "linkend='$$id'>$$ident</link></primaryie></indexentry>" >>$@ ; \
	done)
	@(								\
	echo "</index>") >>$@
