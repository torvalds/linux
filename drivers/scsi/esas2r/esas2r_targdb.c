/*
 *  linux/drivers/scsi/esas2r/esas2r_targdb.c
 *      For use with ATTO ExpressSAS R6xx SAS/SATA RAID controllers
 *
 *  Copyright (c) 2001-2013 ATTO Technology, Inc.
 *  (mailto:linuxdrivers@attotech.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * NO WARRANTY
 * THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
 * LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
 * solely responsible for determining the appropriateness of using and
 * distributing the Program and assumes all risks associated with its
 * exercise of rights under this Agreement, including but not limited to
 * the risks and costs of program errors, damage to or loss of data,
 * programs or equipment, and unavailability or interruption of operations.
 *
 * DISCLAIMER OF LIABILITY
 * NEITHER RECIPIENT NOR ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
 * TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OR DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
 * HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 */

#include "esas2r.h"

void esas2r_targ_db_initialize(struct esas2r_adapter *a)
{
	struct esas2r_target *t;

	for (t = a->targetdb; t < a->targetdb_end; t++) {
		memset(t, 0, sizeof(struct esas2r_target));

		t->target_state = TS_NOT_PRESENT;
		t->buffered_target_state = TS_NOT_PRESENT;
		t->new_target_state = TS_INVALID;
	}
}

void esas2r_targ_db_remove_all(struct esas2r_adapter *a, bool notify)
{
	struct esas2r_target *t;
	unsigned long flags;

	for (t = a->targetdb; t < a->targetdb_end; t++) {
		if (t->target_state != TS_PRESENT)
			continue;

		spin_lock_irqsave(&a->mem_lock, flags);
		esas2r_targ_db_remove(a, t);
		spin_unlock_irqrestore(&a->mem_lock, flags);

		if (notify) {
			esas2r_trace("remove id:%d", esas2r_targ_get_id(t,
									a));
			esas2r_target_state_changed(a, esas2r_targ_get_id(t,
									  a),
						    TS_NOT_PRESENT);
		}
	}
}

void esas2r_targ_db_report_changes(struct esas2r_adapter *a)
{
	struct esas2r_target *t;
	unsigned long flags;

	esas2r_trace_enter();

	if (a->flags & AF_DISC_PENDING) {
		esas2r_trace_exit();
		return;
	}

	for (t = a->targetdb; t < a->targetdb_end; t++) {
		u8 state = TS_INVALID;

		spin_lock_irqsave(&a->mem_lock, flags);
		if (t->buffered_target_state != t->target_state)
			state = t->buffered_target_state = t->target_state;

		spin_unlock_irqrestore(&a->mem_lock, flags);
		if (state != TS_INVALID) {
			esas2r_trace("targ_db_report_changes:%d",
				     esas2r_targ_get_id(
					     t,
					     a));
			esas2r_trace("state:%d", state);

			esas2r_target_state_changed(a,
						    esas2r_targ_get_id(t,
								       a),
						    state);
		}
	}

	esas2r_trace_exit();
}

struct esas2r_target *esas2r_targ_db_add_raid(struct esas2r_adapter *a,
					      struct esas2r_disc_context *
					      dc)
{
	struct esas2r_target *t;

	esas2r_trace_enter();

	if (dc->curr_virt_id >= ESAS2R_MAX_TARGETS) {
		esas2r_bugon();
		esas2r_trace_exit();
		return NULL;
	}

	t = a->targetdb + dc->curr_virt_id;

	if (t->target_state == TS_PRESENT) {
		esas2r_trace_exit();
		return NULL;
	}

	esas2r_hdebug("add RAID %s, T:%d", dc->raid_grp_name,
		      esas2r_targ_get_id(
			      t,
			      a));

	if (dc->interleave == 0
	    || dc->block_size  == 0) {
		/* these are invalid values, don't create the target entry. */

		esas2r_hdebug("invalid RAID group dimensions");

		esas2r_trace_exit();

		return NULL;
	}

	t->block_size = dc->block_size;
	t->inter_byte = dc->interleave;
	t->inter_block = dc->interleave / dc->block_size;
	t->virt_targ_id = dc->curr_virt_id;
	t->phys_targ_id = ESAS2R_TARG_ID_INV;

	t->flags &= ~TF_PASS_THRU;
	t->flags |= TF_USED;

	t->identifier_len = 0;

	t->target_state = TS_PRESENT;

	return t;
}

struct esas2r_target *esas2r_targ_db_add_pthru(struct esas2r_adapter *a,
					       struct esas2r_disc_context *dc,
					       u8 *ident,
					       u8 ident_len)
{
	struct esas2r_target *t;

	esas2r_trace_enter();

	if (dc->curr_virt_id >= ESAS2R_MAX_TARGETS) {
		esas2r_bugon();
		esas2r_trace_exit();
		return NULL;
	}

	/* see if we found this device before. */

	t = esas2r_targ_db_find_by_ident(a, ident, ident_len);

	if (t == NULL) {
		t = a->targetdb + dc->curr_virt_id;

		if (ident_len > sizeof(t->identifier)
		    || t->target_state == TS_PRESENT) {
			esas2r_trace_exit();
			return NULL;
		}
	}

	esas2r_hdebug("add PT; T:%d, V:%d, P:%d", esas2r_targ_get_id(t, a),
		      dc->curr_virt_id,
		      dc->curr_phys_id);

	t->block_size = 0;
	t->inter_byte = 0;
	t->inter_block = 0;
	t->virt_targ_id = dc->curr_virt_id;
	t->phys_targ_id = dc->curr_phys_id;
	t->identifier_len = ident_len;

	memcpy(t->identifier, ident, ident_len);

	t->flags |= TF_PASS_THRU | TF_USED;

	t->target_state = TS_PRESENT;

	return t;
}

void esas2r_targ_db_remove(struct esas2r_adapter *a, struct esas2r_target *t)
{
	esas2r_trace_enter();

	t->target_state = TS_NOT_PRESENT;

	esas2r_trace("remove id:%d", esas2r_targ_get_id(t, a));

	esas2r_trace_exit();
}

struct esas2r_target *esas2r_targ_db_find_by_sas_addr(struct esas2r_adapter *a,
						      u64 *sas_addr)
{
	struct esas2r_target *t;

	for (t = a->targetdb; t < a->targetdb_end; t++)
		if (t->sas_addr == *sas_addr)
			return t;

	return NULL;
}

struct esas2r_target *esas2r_targ_db_find_by_ident(struct esas2r_adapter *a,
						   void *identifier,
						   u8 ident_len)
{
	struct esas2r_target *t;

	for (t = a->targetdb; t < a->targetdb_end; t++) {
		if (ident_len == t->identifier_len
		    && memcmp(&t->identifier[0], identifier,
			      ident_len) == 0)
			return t;
	}

	return NULL;
}

u16 esas2r_targ_db_find_next_present(struct esas2r_adapter *a, u16 target_id)
{
	u16 id = target_id + 1;

	while (id < ESAS2R_MAX_TARGETS) {
		struct esas2r_target *t = a->targetdb + id;

		if (t->target_state == TS_PRESENT)
			break;

		id++;
	}

	return id;
}

struct esas2r_target *esas2r_targ_db_find_by_virt_id(struct esas2r_adapter *a,
						     u16 virt_id)
{
	struct esas2r_target *t;

	for (t = a->targetdb; t < a->targetdb_end; t++) {
		if (t->target_state != TS_PRESENT)
			continue;

		if (t->virt_targ_id == virt_id)
			return t;
	}

	return NULL;
}

u16 esas2r_targ_db_get_tgt_cnt(struct esas2r_adapter *a)
{
	u16 devcnt = 0;
	struct esas2r_target *t;
	unsigned long flags;

	spin_lock_irqsave(&a->mem_lock, flags);
	for (t = a->targetdb; t < a->targetdb_end; t++)
		if (t->target_state == TS_PRESENT)
			devcnt++;

	spin_unlock_irqrestore(&a->mem_lock, flags);

	return devcnt;
}
