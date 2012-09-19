#include "bnx2fc.h"

void BNX2FC_IO_DBG(const struct bnx2fc_cmd *io_req, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (likely(!(bnx2fc_debug_level & LOG_IO)))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (io_req && io_req->port && io_req->port->lport &&
	    io_req->port->lport->host)
		shost_printk(KERN_INFO, io_req->port->lport->host,
			     PFX "xid:0x%x %pV",
			     io_req->xid, &vaf);
	else
		pr_info("NULL %pV", &vaf);

	va_end(args);
}

void BNX2FC_TGT_DBG(const struct bnx2fc_rport *tgt, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (likely(!(bnx2fc_debug_level & LOG_TGT)))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (tgt && tgt->port && tgt->port->lport && tgt->port->lport->host &&
	    tgt->rport)
		shost_printk(KERN_INFO, tgt->port->lport->host,
			     PFX "port:%x %pV",
			     tgt->rport->port_id, &vaf);
	else
		pr_info("NULL %pV", &vaf);

	va_end(args);
}

void BNX2FC_HBA_DBG(const struct fc_lport *lport, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	if (likely(!(bnx2fc_debug_level & LOG_HBA)))
		return;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	if (lport && lport->host)
		shost_printk(KERN_INFO, lport->host, PFX "%pV", &vaf);
	else
		pr_info("NULL %pV", &vaf);

	va_end(args);
}
