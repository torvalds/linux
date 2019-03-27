LDEMUL_BEFORE_PARSE=gldnetbsd_before_parse

cat >>e${EMULATION_NAME}.c <<EOF
static void
gld${EMULATION_NAME}_before_parse (void);

static void
gldnetbsd_before_parse (void)
{
  gld${EMULATION_NAME}_before_parse ();
  link_info.common_skip_ar_aymbols = bfd_link_common_skip_text;
}
EOF
