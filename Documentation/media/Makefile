# Rules to convert DOT and SVG to Sphinx images

SRC_DIR=$(srctree)/Documentation/media

DOTS = \
	uapi/v4l/pipeline.dot \

IMAGES = \
	typical_media_device.svg \
	uapi/dvb/dvbstb.svg \
	uapi/v4l/bayer.svg \
	uapi/v4l/constraints.svg \
	uapi/v4l/crop.svg \
	uapi/v4l/fieldseq_bt.svg \
	uapi/v4l/fieldseq_tb.svg \
	uapi/v4l/nv12mt.svg \
	uapi/v4l/nv12mt_example.svg \
	uapi/v4l/pipeline.svg \
	uapi/v4l/selection.svg \
	uapi/v4l/subdev-image-processing-full.svg \
	uapi/v4l/subdev-image-processing-scaling-multi-source.svg \
	uapi/v4l/subdev-image-processing-crop.svg \
	uapi/v4l/vbi_525.svg \
	uapi/v4l/vbi_625.svg \
	uapi/v4l/vbi_hsync.svg \

DOTTGT := $(patsubst %.dot,%.svg,$(DOTS))
IMGDOT := $(patsubst %,$(SRC_DIR)/%,$(DOTTGT))

IMGTGT := $(patsubst %.svg,%.pdf,$(IMAGES))
IMGPDF := $(patsubst %,$(SRC_DIR)/%,$(IMGTGT))

cmd = $(echo-cmd) $(cmd_$(1))

quiet_cmd_genpdf = GENPDF  $2
      cmd_genpdf = convert $2 $3

quiet_cmd_gendot = DOT     $2
      cmd_gendot = dot -Tsvg $2 > $3

%.pdf: %.svg
	@$(call cmd,genpdf,$<,$@)

%.svg: %.dot
	@$(call cmd,gendot,$<,$@)

# Rules to convert a .h file to inline RST documentation

PARSER = $(srctree)/Documentation/sphinx/parse-headers.pl
UAPI = $(srctree)/include/uapi/linux
KAPI = $(srctree)/include/linux

FILES = audio.h.rst ca.h.rst dmx.h.rst frontend.h.rst net.h.rst video.h.rst \
	  videodev2.h.rst media.h.rst cec.h.rst lirc.h.rst

TARGETS := $(addprefix $(BUILDDIR)/, $(FILES))

gen_rst = \
	echo ${PARSER} $< $@ $(SRC_DIR)/$(notdir $@).exceptions; \
	${PARSER} $< $@ $(SRC_DIR)/$(notdir $@).exceptions

quiet_gen_rst = echo '  PARSE   $(patsubst $(srctree)/%,%,$<)'; \
	${PARSER} $< $@ $(SRC_DIR)/$(notdir $@).exceptions

silent_gen_rst = ${gen_rst}

$(BUILDDIR)/audio.h.rst: ${UAPI}/dvb/audio.h ${PARSER} $(SRC_DIR)/audio.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/ca.h.rst: ${UAPI}/dvb/ca.h ${PARSER} $(SRC_DIR)/ca.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/dmx.h.rst: ${UAPI}/dvb/dmx.h ${PARSER} $(SRC_DIR)/dmx.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/frontend.h.rst: ${UAPI}/dvb/frontend.h ${PARSER} $(SRC_DIR)/frontend.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/net.h.rst: ${UAPI}/dvb/net.h ${PARSER} $(SRC_DIR)/net.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/video.h.rst: ${UAPI}/dvb/video.h ${PARSER} $(SRC_DIR)/video.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/videodev2.h.rst: ${UAPI}/videodev2.h ${PARSER} $(SRC_DIR)/videodev2.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/media.h.rst: ${UAPI}/media.h ${PARSER} $(SRC_DIR)/media.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/cec.h.rst: ${UAPI}/cec.h ${PARSER} $(SRC_DIR)/cec.h.rst.exceptions
	@$($(quiet)gen_rst)

$(BUILDDIR)/lirc.h.rst: ${UAPI}/lirc.h ${PARSER} $(SRC_DIR)/lirc.h.rst.exceptions
	@$($(quiet)gen_rst)

# Media build rules

.PHONY: all html epub xml latex

all: $(IMGDOT) $(BUILDDIR) ${TARGETS}
html: all
epub: all
xml: all
latex: $(IMGPDF) all

clean:
	-rm -f $(DOTTGT) $(IMGTGT) ${TARGETS} 2>/dev/null

$(BUILDDIR):
	$(Q)mkdir -p $@
