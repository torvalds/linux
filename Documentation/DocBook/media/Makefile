###
# Media build rules - Auto-generates media contents/indexes and *.h xml's
#

SHELL=/bin/bash

MEDIA_OBJ_DIR=$(objtree)/Documentation/DocBook/
MEDIA_SRC_DIR=$(srctree)/Documentation/DocBook/media

MEDIA_TEMP =  media-entities.tmpl \
	      media-indices.tmpl \
	      videodev2.h.xml \
	      v4l2.xml \
	      audio.h.xml \
	      ca.h.xml \
	      dmx.h.xml \
	      frontend.h.xml \
	      net.h.xml \
	      video.h.xml \

IMGFILES := $(patsubst %.b64,%, $(notdir $(shell ls $(MEDIA_SRC_DIR)/*.b64)))
OBJIMGFILES := $(addprefix $(MEDIA_OBJ_DIR)/, $(IMGFILES))
GENFILES := $(addprefix $(MEDIA_OBJ_DIR)/, $(MEDIA_TEMP))

PHONY += cleanmediadocs

cleanmediadocs:
	-@rm -f `find $(MEDIA_OBJ_DIR) -type l` $(GENFILES) $(OBJIMGFILES) 2>/dev/null

$(obj)/media_api.xml: $(GENFILES) FORCE

#$(MEDIA_OBJ_DIR)/media_api.html: $(MEDIA_OBJ_DIR)/media_api.xml
#$(MEDIA_OBJ_DIR)/media_api.pdf: $(MEDIA_OBJ_DIR)/media_api.xml
#$(MEDIA_OBJ_DIR)/media_api.ps: $(MEDIA_OBJ_DIR)/media_api.xml

V4L_SGMLS = \
	$(shell ls $(MEDIA_SRC_DIR)/v4l/*.xml|perl -ne 'print "$$1 " if (m,.*/(.*)\n,)') \
	capture.c.xml \
	keytable.c.xml \
	v4l2grab.c.xml

DVB_SGMLS = \
	$(shell ls $(MEDIA_SRC_DIR)/dvb/*.xml|perl -ne 'print "$$1 " if (m,.*/(.*)\n,)')

MEDIA_SGMLS =  $(addprefix ./,$(V4L_SGMLS)) $(addprefix ./,$(DVB_SGMLS)) $(addprefix ./,$(MEDIA_TEMP))

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
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/videodev2.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/dvb/audio.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/dvb/ca.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/dvb/dmx.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/dvb/frontend.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([A-Z][^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/dvb/net.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/dvb/video.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/media.h) \
	$(shell perl -ne 'print "$$1 " if /\#define\s+([^\s]+)\s+_IO/' $(srctree)/include/uapi/linux/v4l2-subdev.h) \

DEFINES = \
	$(shell perl -ne 'print "$$1 " if /\#define\s+(DTV_[^\s]+)\s+/' $(srctree)/include/uapi/linux/dvb/frontend.h) \

TYPES = \
	$(shell perl -ne 'print "$$1 " if /^typedef\s+.*\s+(\S+)\;/' $(srctree)/include/uapi/linux/videodev2.h) \
	$(shell perl -ne 'print "$$1 " if /^typedef\s+.*\s+(\S+)\;/' $(srctree)/include/uapi/linux/dvb/frontend.h)

ENUMS = \
	$(shell perl -ne 'print "$$1 " if /^enum\s+([^\s]+)\s+/' \
		$(srctree)/include/uapi/linux/videodev2.h \
		$(srctree)/include/uapi/linux/dvb/audio.h \
		$(srctree)/include/uapi/linux/dvb/ca.h \
		$(srctree)/include/uapi/linux/dvb/dmx.h \
		$(srctree)/include/uapi/linux/dvb/frontend.h \
		$(srctree)/include/uapi/linux/dvb/net.h \
		$(srctree)/include/uapi/linux/dvb/video.h \
		$(srctree)/include/uapi/linux/media.h \
		$(srctree)/include/uapi/linux/v4l2-mediabus.h \
		$(srctree)/include/uapi/linux/v4l2-subdev.h)

ENUM_DEFS = \
	$(shell perl -e 'open IN,"cat @ARGV| cpp -fpreprocessed |"; while (<IN>) { if ($$enum) {print "$$1\n" if (/\s*([A-Z]\S+)\b/); } $$enum = 0 if ($$enum && /^\}/); $$enum = 1 if(/^\s*enum\s/); }; close IN;' \
		$(srctree)/include/uapi/linux/dvb/dmx.h \
		$(srctree)/include/uapi/linux/dvb/frontend.h)

STRUCTS = \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/uapi/linux/videodev2.h) \
	$(shell perl -ne 'print "$$1 " if (/^struct\s+([^\s\{]+)\s*/)' $(srctree)/include/uapi/linux/dvb/audio.h) \
	$(shell perl -ne 'print "$$1 " if (/^struct\s+([^\s]+)\s+/)' $(srctree)/include/uapi/linux/dvb/ca.h) \
	$(shell perl -ne 'print "$$1 " if (/^struct\s+([^\s]+)\s+/)' $(srctree)/include/uapi/linux/dvb/dmx.h) \
	$(shell perl -ne 'print "$$1 " if (!/dtv\_cmds\_h/ && /^struct\s+([^\s]+)\s+/)' $(srctree)/include/uapi/linux/dvb/frontend.h) \
	$(shell perl -ne 'print "$$1 " if (/^struct\s+([^\s]+)\s+/ && !/_old/)' $(srctree)/include/uapi/linux/dvb/net.h) \
	$(shell perl -ne 'print "$$1 " if (/^struct\s+([^\s]+)\s+/)' $(srctree)/include/uapi/linux/dvb/video.h) \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/uapi/linux/media.h) \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/uapi/linux/v4l2-subdev.h) \
	$(shell perl -ne 'print "$$1 " if /^struct\s+([^\s]+)\s+/' $(srctree)/include/uapi/linux/v4l2-mediabus.h)

ERRORS = \
	E2BIG \
	EACCES \
	EAGAIN \
	EBADF \
	EBADFD \
	EBADR \
	EBADRQC \
	EBUSY \
	ECHILD \
	ECONNRESET \
	EDEADLK \
	EDOM \
	EEXIST \
	EFAULT \
	EFBIG \
	EILSEQ \
	EINIT \
	EINPROGRESS \
	EINTR \
	EINVAL \
	EIO \
	EMFILE \
	ENFILE \
	ENOBUFS \
	ENODATA \
	ENODEV \
	ENOENT \
	ENOIOCTLCMD \
	ENOMEM \
	ENOSPC \
	ENOSR \
	ENOSYS \
	ENOTSUP \
	ENOTSUPP \
	ENOTTY \
	ENXIO \
	EOPNOTSUPP \
	EOVERFLOW \
	EPERM \
	EPIPE \
	EPROTO \
	ERANGE \
	EREMOTE \
	EREMOTEIO \
	ERESTART \
	ERESTARTSYS \
	ESHUTDOWN \
	ESPIPE \
	ETIME \
	ETIMEDOUT \
	EUSERS \
	EWOULDBLOCK \
	EXDEV \

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
	-e "s/\(V4L2_PIX_FMT_[A-Z0-9_]\+\)\(\s\+v4l2_fourcc\)/<link linkend=\"\1\">\1<\/link>\2/g" \
	-e ":a;s/\(linkend=\".*\)_\(.*\">\)/\1-\2/;ta" \
	-e "s/v4l2\-mpeg\-vbi\-ITV0/v4l2-mpeg-vbi-itv0-1/g"

DVB_DOCUMENTED = \
	-e "s,\(struct\s\+\)\([a-z0-9_]\+\)\(\s\+{\),\1\<link linkend=\"\2\">\2\<\/link\>\3,g" \
	-e "s,\(}\s\+\)\([a-z0-9_]\+_t\+\),\1\<link linkend=\"\2\">\2\<\/link\>,g" \
	-e "s,\(define\s\+\)\(DTV_[A-Z0-9_]\+\)\(\s\+[0-9]\+\),\1\<link linkend=\"\2\">\2\<\/link\>\3,g" \
	-e "s,<link\s\+linkend=\".*\">\(DTV_IOCTL_MAX_MSGS\|dtv_cmds_h\|__.*_old\)<\/link>,\1,g" \
	-e ":a;s/\(linkend=\".*\)_\(.*\">\)/\1-\2/;ta" \
	-e "s,\(audio-mixer\|audio-karaoke\|audio-status\|ca-slot-info\|ca-descr-info\|ca-caps\|ca-msg\|ca-descr\|ca-pid\|dmx-filter\|dmx-caps\|video-system\|video-highlight\|video-spu\|video-spu-palette\|video-navi-pack\)-t,\1,g" \
	-e "s,DTV-ISDBT-LAYER[A-C],DTV-ISDBT-LAYER,g" \
	-e "s,\(define\s\+\)\([A-Z0-9_]\+\)\(\s\+_IO\),\1\<link linkend=\"\2\">\2\<\/link\>\3,g" \
	-e "s,\(define\s\+\)\(DTV_[A-Z0-9_]\+\)\(\s\+\),\1\<link linkend=\"\2\">\2\<\/link\>\3,g" \
	-e "s,<link\s\+linkend=\".*\">\(__.*_OLD\)<\/link>,\1,g" \
	-e "s/\(linkend\=\"\)FE_SET_PROPERTY/\1FE_GET_PROPERTY/g" \
	-e "s,<link\s\+linkend=\".*\">\(DTV_ISDBS_TS_ID_LEGACY\|DTV_MAX_COMMAND\|DTV_IOCTL_MAX_MSGS\)<\/link>,\1,g" \

#
# Media targets and dependencies
#

install_media_images = \
	$(Q)if [ "x$(findstring media_api.xml,$(DOCBOOKS))" != "x" ]; then \
		mkdir -p $(MEDIA_OBJ_DIR)/media_api; \
		cp $(OBJIMGFILES) $(MEDIA_SRC_DIR)/*.svg $(MEDIA_SRC_DIR)/v4l/*.svg $(MEDIA_OBJ_DIR)/media_api; \
	fi

$(MEDIA_OBJ_DIR)/%: $(MEDIA_SRC_DIR)/%.b64
	$(Q)base64 -d $< >$@

$(MEDIA_OBJ_DIR)/v4l2.xml: $(OBJIMGFILES)
	@$($(quiet)gen_xml)
	@(ln -sf `cd $(MEDIA_SRC_DIR) && /bin/pwd`/v4l/*xml $(MEDIA_OBJ_DIR)/)
	@(ln -sf `cd $(MEDIA_SRC_DIR) && /bin/pwd`/dvb/*xml $(MEDIA_OBJ_DIR)/)

$(MEDIA_OBJ_DIR)/videodev2.h.xml: $(srctree)/include/uapi/linux/videodev2.h $(MEDIA_OBJ_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DOCUMENTED) |		\
	  sed 's/i\.e\./&ie;/') >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_OBJ_DIR)/audio.h.xml: $(srctree)/include/uapi/linux/dvb/audio.h $(MEDIA_OBJ_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DVB_DOCUMENTED) |	\
	  sed 's/i\.e\./&ie;/') >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_OBJ_DIR)/ca.h.xml: $(srctree)/include/uapi/linux/dvb/ca.h $(MEDIA_OBJ_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DVB_DOCUMENTED) |	\
	  sed 's/i\.e\./&ie;/') >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_OBJ_DIR)/dmx.h.xml: $(srctree)/include/uapi/linux/dvb/dmx.h $(MEDIA_OBJ_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	for ident in $(ENUM_DEFS) ; do		\
	  entity=`echo $$ident | tr _ -` ;	\
	  r="$$r s/([^\w\-])$$ident([^\w\-])/\1\&$$entity\;\2/g;";\
	done;					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DVB_DOCUMENTED) |	\
	  sed 's/i\.e\./&ie;/' |		\
	  perl -ne "$$r print $$_;") >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_OBJ_DIR)/frontend.h.xml: $(srctree)/include/uapi/linux/dvb/frontend.h $(MEDIA_OBJ_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	for ident in $(ENUM_DEFS) ; do		\
	  entity=`echo $$ident | tr _ -` ;	\
	  r="$$r s/([^\w\-])$$ident([^\w\-])/\1\&$$entity\;\2/g;";\
	done;					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DVB_DOCUMENTED) |	\
	  sed 's/i\.e\./&ie;/' |		\
	  perl -ne "$$r print $$_;") >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_OBJ_DIR)/net.h.xml: $(srctree)/include/uapi/linux/dvb/net.h $(MEDIA_OBJ_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DVB_DOCUMENTED) |	\
	  sed 's/i\.e\./&ie;/') >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_OBJ_DIR)/video.h.xml: $(srctree)/include/uapi/linux/dvb/video.h $(MEDIA_OBJ_DIR)/v4l2.xml
	@$($(quiet)gen_xml)
	@(					\
	echo "<programlisting>") > $@
	@(					\
	expand --tabs=8 < $< |			\
	  sed $(ESCAPE) $(DVB_DOCUMENTED) |	\
	  sed 's/i\.e\./&ie;/') >> $@
	@(					\
	echo "</programlisting>") >> $@

$(MEDIA_OBJ_DIR)/media-entities.tmpl: $(MEDIA_OBJ_DIR)/v4l2.xml
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
	for ident in `echo $(IOCTLS) | sed -e "s,VIDIOC_RESERVED,,"`; do\
	  entity=`echo $$ident | tr _ -` ;				\
	  id=`grep -e "<refname>$$ident" -e "<section id=\"$$ident\"" $$(find $(MEDIA_SRC_DIR) -name *.xml -type f)| sed -r s,"^.*/(.*).xml.*","\1",` ; \
	  if [ "$$id" != "" ]; then echo "<!ENTITY $$entity \"<link"	\
	    "linkend='$$id'><constant>$$ident</constant></link>\">"	\
	  >>$@ ; else							\
		echo "Warning: undocumented ioctl: $$ident. Please document it at the media DocBook!" >&2;	\
	  fi;								\
	done)
	@(								\
	echo -e "\n<!-- Defines -->") >>$@
	@(								\
	for ident in $(DEFINES) ; do					\
	  entity=`echo $$ident | tr _ -` ;				\
	  echo "<!ENTITY $$entity \"<link"				\
	    "linkend='$$entity'><constant>$$ident</constant></link>\">"	\
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
	echo -e "\n<!-- Enum definitions -->") >>$@
	@(								\
	for ident in $(ENUM_DEFS) ; do					\
	  entity=`echo $$ident | tr _ -` ;				\
	  echo "<!ENTITY $$entity \"<link"				\
	    "linkend='$$entity'><constant>$$ident</constant></link>\">"	\
	  >>$@ ;							\
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
$(MEDIA_OBJ_DIR)/media-indices.tmpl: $(MEDIA_OBJ_DIR)/v4l2.xml
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

