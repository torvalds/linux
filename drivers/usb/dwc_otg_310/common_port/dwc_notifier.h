
#ifndef __DWC_NOTIFIER_H__
#define __DWC_NOTIFIER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "dwc_os.h"

/** @file
 *
 * A simple implementation of the Observer pattern.  Any "module" can
 * register as an observer or notifier.  The notion of "module" is abstract and
 * can mean anything used to identify either an observer or notifier.  Usually
 * it will be a pointer to a data structure which contains some state, ie an
 * object.
 *
 * Before any notifiers can be added, the global notification manager must be
 * brought up with dwc_alloc_notification_manager().
 * dwc_free_notification_manager() will bring it down and free all resources.
 * These would typically be called upon module load and unload.  The
 * notification manager is a single global instance that handles all registered
 * observable modules and observers so this should be done only once.
 *
 * A module can be observable by using Notifications to publicize some general
 * information about it's state or operation.  It does not care who listens, or
 * even if anyone listens, or what they do with the information.  The observable
 * modules do not need to know any information about it's observers or their
 * interface, or their state or data.
 *
 * Any module can register to emit Notifications.  It should publish a list of
 * notifications that it can emit and their behavior, such as when they will get
 * triggered, and what information will be provided to the observer.  Then it
 * should register itself as an observable module. See dwc_register_notifier().
 *
 * Any module can observe any observable, registered module, provided it has a
 * handle to the other module and knows what notifications to observe.  See
 * dwc_add_observer().
 *
 * A function of type dwc_notifier_callback_t is called whenever a notification
 * is triggered with one or more observers observing it.  This function is
 * called in it's own process so it may sleep or block if needed.  It is
 * guaranteed to be called sometime after the notification has occurred and will
 * be called once per each time the notification is triggered.  It will NOT be
 * called in the same process context used to trigger the notification.
 *
 * @section Limitiations
 *
 * Keep in mind that Notifications that can be triggered in rapid sucession may
 * schedule too many processes too handle.  Be aware of this limitation when
 * designing to use notifications, and only add notifications for appropriate
 * observable information.
 *
 * Also Notification callbacks are not synchronous.  If you need to synchronize
 * the behavior between module/observer you must use other means.  And perhaps
 * that will mean Notifications are not the proper solution.
 */

struct dwc_notifier;
typedef struct dwc_notifier dwc_notifier_t;

/** The callback function must be of this type.
 *
 * @param object This is the object that is being observed.
 * @param notification This is the notification that was triggered.
 * @param observer This is the observer
 * @param notification_data This is notification-specific data that the notifier
 * has included in this notification.  The value of this should be published in
 * the documentation of the observable module with the notifications.
 * @param user_data This is any custom data that the observer provided when
 * adding itself as an observer to the notification. */
typedef void (*dwc_notifier_callback_t)(void *object, char *notification, void *observer,
					void *notification_data, void *user_data);

/** Brings up the notification manager. */
extern int dwc_alloc_notification_manager(void *mem_ctx, void *wkq_ctx);
/** Brings down the notification manager. */
extern void dwc_free_notification_manager(void);

/** This function registers an observable module.  A dwc_notifier_t object is
 * returned to the observable module.  This is an opaque object that is used by
 * the observable module to trigger notifications.  This object should only be
 * accessible to functions that are authorized to trigger notifications for this
 * module.  Observers do not need this object. */
extern dwc_notifier_t *dwc_register_notifier(void *mem_ctx, void *object);

/** This function unregisters an observable module.  All observers have to be
 * removed prior to unregistration. */
extern void dwc_unregister_notifier(dwc_notifier_t *notifier);

/** Add a module as an observer to the observable module.  The observable module
 * needs to have previously registered with the notification manager.
 *
 * @param observer The observer module
 * @param object The module to observe
 * @param notification The notification to observe
 * @param callback The callback function to call
 * @param user_data Any additional user data to pass into the callback function */
extern int dwc_add_observer(void *observer, void *object, char *notification,
			    dwc_notifier_callback_t callback, void *user_data);

/** Removes the specified observer from all notifications that it is currently
 * observing. */
extern int dwc_remove_observer(void *observer);

/** This function triggers a Notification.  It should be called by the
 * observable module, or any module or library which the observable module
 * allows to trigger notification on it's behalf.  Such as the dwc_cc_t.
 *
 * dwc_notify is a non-blocking function.  Callbacks are scheduled called in
 * their own process context for each trigger.  Callbacks can be blocking.
 * dwc_notify can be called from interrupt context if needed.
 *
 */
void dwc_notify(dwc_notifier_t *notifier, char *notification, void *notification_data);

#ifdef __cplusplus
}
#endif

#endif /* __DWC_NOTIFIER_H__ */
