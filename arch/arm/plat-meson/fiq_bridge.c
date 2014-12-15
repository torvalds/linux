#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <plat/fiq_bridge.h>

static irqreturn_t root_handle_isr(int irq, void *handle)
{
    bridge_item_t  *pitem;

    list_for_each_entry(pitem, &fiq_bridge_list, list) {
        if (pitem->active && pitem->handle) {
            pitem->handle(irq, (void*)pitem->key);
            pitem->active = 0;
        }

    }
    return IRQ_HANDLED;
}

int  fiq_bridge_pulse_trigger(bridge_item_t *c_item)
{
    c_item->active = 1;
    BRIDGE_IRQ_SET();
    return 0;
}
int  register_fiq_bridge_handle(bridge_item_t *c_item)
{
    bridge_item_t  *pitem;

    if (NULL == c_item) {
        return -1;
    }
    //gurantee not exist
    list_for_each_entry(pitem, &fiq_bridge_list, list) {
        if (pitem->key == c_item->key || pitem->handle == c_item->handle) {
            return -1;
        }

    }
    c_item->active = 0;
    if (list_empty(&fiq_bridge_list)) {
    		aml_clr_reg32_mask(P_ISA_TIMER_MUX, (1<<18)|(1<<14)|(3<<4));
		aml_set_reg32_mask(P_ISA_TIMER_MUX,	(1<<18)|(0<<14)|(0<<4));   		
        if (request_irq(BRIDGE_IRQ, &root_handle_isr,
                        IRQF_SHARED , "fiq_bridge", &fiq_bridge_list) < 0) {
            printk("can't not register  fiq bridge handle %s\n", c_item->name);
            return -1;
        }
    }
    list_add(&c_item->list, &fiq_bridge_list);

    return 0;
}
int  unregister_fiq_bridge_handle(bridge_item_t *c_item)
{
    bridge_item_t  *pitem, *tmp;


    if (NULL == c_item) {
        return -1;
    }
    list_for_each_entry_safe(pitem, tmp, &fiq_bridge_list, list) {
        if (pitem->key == c_item->key &&  pitem->handle == c_item->handle) {
            list_del(&pitem->list);
            break;
        }

    }
    if (list_empty(&fiq_bridge_list)) {
        free_irq(BRIDGE_IRQ, &fiq_bridge_list);
    }
    return 0;
}

