/**
 ******************************************************************************
 *
 * @file ecrnx_mu_group.c
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#include "ecrnx_defs.h"
#include "ecrnx_msg_tx.h"
#include "ecrnx_events.h"


/**
 * ecrnx_mu_group_sta_init - Initialize group information for a STA
 *
 * @sta: Sta to initialize
 */
void ecrnx_mu_group_sta_init(struct ecrnx_sta *sta,
                            const struct ieee80211_vht_cap *vht_cap)
{
    sta->group_info.map = 0;
    sta->group_info.cnt = 0;
    sta->group_info.active.next = LIST_POISON1;
    sta->group_info.update.next = LIST_POISON1;
    sta->group_info.last_update = 0;
    sta->group_info.traffic = 0;
    sta->group_info.group = 0;

    if (!vht_cap ||
        !(vht_cap->vht_cap_info & IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE)) {
            sta->group_info.map = ECRNX_SU_GROUP;
    }
}

/**
 * ecrnx_mu_group_sta_del - Remove a sta from all MU group
 *
 * @ecrnx_hw: main driver data
 * @sta: STA to remove
 *
 * Remove one sta from all the MU groups it belongs to.
 */
void ecrnx_mu_group_sta_del(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta)
{
    struct ecrnx_mu_info *mu = &ecrnx_hw->mu;
    int i, j, group_id;
    bool lock_taken;
    u64 map;

    lock_taken = (down_interruptible(&mu->lock) == 0);

    group_sta_for_each(sta, group_id, map) {
        struct ecrnx_mu_group *group = ecrnx_mu_group_from_id(mu, group_id);

        for (i = 0; i < CONFIG_USER_MAX; i++) {
            if (group->users[i] == sta) {
                group->users[i] = NULL;
                group->user_cnt --;
                /* Don't keep group with only one user */
                if (group->user_cnt == 1) {
                    for (j = 0; j < CONFIG_USER_MAX; j++) {
                        if (group->users[j]) {
                            group->users[j]->group_info.cnt--;
                            group->users[j]->group_info.map &= ~BIT_ULL(group->group_id);
                            if (group->users[j]->group_info.group == group_id)
                                group->users[j]->group_info.group = 0;
                            group->user_cnt --;
                            break;
                        }
                    }
                    mu->group_cnt--;
                    trace_mu_group_delete(group->group_id);
                } else {
                    trace_mu_group_update(group);
                }
                break;
            }
        }

        WARN((i == CONFIG_USER_MAX), "sta %d doesn't belongs to group %d",
            sta->sta_idx, group_id);
    }

    sta->group_info.map = 0;
    sta->group_info.cnt = 0;
    sta->group_info.traffic = 0;

    if (sta->group_info.active.next != LIST_POISON1)
        list_del(&sta->group_info.active);

    if (sta->group_info.update.next != LIST_POISON1)
        list_del(&sta->group_info.update);

    if (lock_taken)
        up(&mu->lock);
}

/**
 * ecrnx_mu_group_sta_get_map - Get the list of group a STA belongs to
 *
 * @sta: pointer to the sta
 *
 * @return the list of group a STA belongs to as a bitfield
 */
u64 ecrnx_mu_group_sta_get_map(struct ecrnx_sta *sta)
{
    if (sta)
        return sta->group_info.map;
    return 0;
}

/**
 * ecrnx_mu_group_sta_get_pos - Get sta position in a group
 *
 * @ecrnx_hw: main driver data
 * @sta: pointer to the sta
 * @group_id: Group id
 *
 * @return the positon of @sta in group @group_id or -1 if the sta
 * doesn't belongs to the group (or group id is invalid)
 */
int ecrnx_mu_group_sta_get_pos(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta,
                              int group_id)
{
    struct ecrnx_mu_group *group;
    int i;

    group = ecrnx_mu_group_from_id(&ecrnx_hw->mu, group_id);
    if (!group)
        return -1;

    for (i = 0; i < CONFIG_USER_MAX; i++) {
        if (group->users[i] == sta)
            return i;
    }

    WARN(1, "sta %d doesn't belongs to group %d",
         sta->sta_idx, group_id);
    return -1;
}

/**
 * ecrnx_mu_group_move_head - Move (or add) one element at the top of a list
 *
 * @list: list pointer
 * @elem: element to move (or add) at the top of @list
 *
 */
static inline
void ecrnx_mu_group_move_head(struct list_head *list, struct list_head *elem)
{
    if (elem->next != LIST_POISON1) {
        __list_del_entry(elem);
    }
    list_add(elem, list);
}

/**
 * ecrnx_mu_group_remove_users - Remove all the users of a group
 *
 * @mu: pointer on MU info
 * @group: pointer on group to remove users from
 *
 * Loop over all users one one group and remove this group from their
 * map (and count).
 * Each users is also added to the update_sta list, so that group info
 * will be resent to fw for this user.
 */
static inline
void ecrnx_mu_group_remove_users(struct ecrnx_mu_info *mu,
                                struct ecrnx_mu_group *group)
{
    struct ecrnx_sta *sta;
    int i, group_id = group->group_id;

    for (i = 0; i < CONFIG_USER_MAX; i++) {
        if (group->users[i]) {
            sta = group->users[i];
            group->users[i] = NULL;
            sta->group_info.cnt--;
            sta->group_info.map &= ~BIT_ULL(group_id);
            ecrnx_mu_group_move_head(&mu->update_sta,
                                    &sta->group_info.update);
        }
    }

    if (group->user_cnt)
        mu->group_cnt--;
    group->user_cnt = 0;
    trace_mu_group_delete(group_id);
}

/**
 * ecrnx_mu_group_add_users - Add users to a group
 *
 * @mu: pointer on MU info
 * @group: pointer on group to add users in
 * @nb_user: number of users to ad
 * @users: table of user to add
 *
 * Add @nb_users to @group (which may already have users)
 * Each new users is added to the first free position.
 * It is assume that @group has at least @nb_user free position. If it is not
 * case it only add the number of users needed to complete the group.
 * Each users (effectively added to @group) is also added to the update_sta
 * list, so that group info will be resent to fw for this user.
 */
static inline
void ecrnx_mu_group_add_users(struct ecrnx_mu_info *mu,
                             struct ecrnx_mu_group *group,
                             int nb_user, struct ecrnx_sta **users)
{
    int i, j, group_id = group->group_id;

    if (!group->user_cnt)
        mu->group_cnt++;

    j = 0;
    for (i = 0; i < nb_user ; i++) {
        for (; j < CONFIG_USER_MAX ; j++) {
            if (group->users[j] == NULL) {
                group->users[j] = users[i];
                users[i]->group_info.cnt ++;
                users[i]->group_info.map |= BIT_ULL(group_id);

                ecrnx_mu_group_move_head(&(mu->update_sta),
                                        &(users[i]->group_info.update));
                group->user_cnt ++;
                j ++;
                break;
            }

            WARN(j == (CONFIG_USER_MAX - 1),
                 "Too many user for group %d (nb_user=%d)",
                 group_id, group->user_cnt + nb_user - i);
        }
    }

    trace_mu_group_update(group);
}


/**
 * ecrnx_mu_group_create_one - create on group with a specific group of user
 *
 * @mu: pointer on MU info
 * @nb_user: number of user to include in the group (<= CONFIG_USER_MAX)
 * @users: table of users
 *
 * Try to create a new group with a specific group of users.
 * 1- First it checks if a group containing all this users already exists.
 *
 * 2- Then it checks if it is possible to complete a group which already
 *    contains at least one user.
 *
 * 3- Finally it create a new group. To do so, it take take the last group of
 *    the active_groups list, remove all its current users and add the new ones
 *
 * In all cases, the group selected is moved at the top of the active_groups
 * list
 *
 * @return 1 if a new group has been created and 0 otherwise
 */
static
int ecrnx_mu_group_create_one(struct ecrnx_mu_info *mu, int nb_user,
                             struct ecrnx_sta **users, int *nb_group_left)
{
    int i, group_id;
    struct ecrnx_mu_group *group;
    u64 group_match;
    u64 group_avail;

    group_match = users[0]->group_info.map;
    group_avail = users[0]->group_info.map;
    for (i = 1; i < nb_user ; i++) {
        group_match &= users[i]->group_info.map;
        group_avail |= users[i]->group_info.map;

    }

    if (group_match) {
        /* a group (or more) with all the users already exist */
        group_id = ECRNX_GET_FIRST_GROUP_ID(group_match);
        group = ecrnx_mu_group_from_id(mu, group_id);
        ecrnx_mu_group_move_head(&mu->active_groups, &group->list);
        return 0;
    }

#if CONFIG_USER_MAX > 2
    if (group_avail) {
        /* check if we can complete a group */
        struct ecrnx_sta *users2[CONFIG_USER_MAX];
        int nb_user2;

        group_for_each(group_id, group_avail) {
            group = ecrnx_mu_group_from_id(mu, group_id);
            if (group->user_cnt == CONFIG_USER_MAX)
                continue;

            nb_user2 = 0;
            for (i = 0; i < nb_user ; i++) {
                if (!(users[i]->group_info.map & BIT_ULL(group_id))) {
                    users2[nb_user2] = users[i];
                    nb_user2++;
                }
            }

            if ((group->user_cnt + nb_user2) <= CONFIG_USER_MAX) {
                ecrnx_mu_group_add_users(mu, group, nb_user2, users2);
                ecrnx_mu_group_move_head(&mu->active_groups, &group->list);
                return 0;
            }
        }
    }
#endif /* CONFIG_USER_MAX > 2*/

    /* create a new group */
    group = list_last_entry(&mu->active_groups, struct ecrnx_mu_group, list);
    ecrnx_mu_group_remove_users(mu, group);
    ecrnx_mu_group_add_users(mu, group, nb_user, users);
    ecrnx_mu_group_move_head(&mu->active_groups, &group->list);
    (*nb_group_left)--;

    return 1;
}

/**
 * ecrnx_mu_group_create - Create new groups containing one specific sta
 *
 * @mu: pointer on MU info
 * @sta: sta to add in each group
 * @nb_group_left: maximum number to new group allowed. (updated on exit)
 *
 * This will try to create "all the possible" group with a specific sta being
 * a member of all these group.
 * The function simply loops over the @active_sta list (starting from @sta).
 * When it has (CONFIG_USER_MAX - 1) users it try to create a new group with
 * these users (plus @sta).
 * Loops end when there is no more users, or no more new group is allowed
 *
 */
static
void ecrnx_mu_group_create(struct ecrnx_mu_info *mu, struct ecrnx_sta *sta,
                          int *nb_group_left)
{
    struct ecrnx_sta *user_sta = sta;
    struct ecrnx_sta *users[CONFIG_USER_MAX];
    int nb_user = 1;

    users[0] = sta;
    while (*nb_group_left) {

        list_for_each_entry_continue(user_sta, &mu->active_sta, group_info.active) {
            users[nb_user] = user_sta;
            if (++nb_user == CONFIG_USER_MAX) {
                break;
            }
        }

        if (nb_user > 1) {
            if (ecrnx_mu_group_create_one(mu, nb_user, users, nb_group_left))
                (*nb_group_left)--;

            if (nb_user < CONFIG_USER_MAX)
                break;
            else
                nb_user = 1;
        } else
            break;
    }
}

/**
 * ecrnx_mu_group_work - process function of the "group_work"
 *
 * The work is scheduled when several sta (MU beamformee capable) are active.
 * When called, the @active_sta contains the list of the active sta (starting
 * from the most recent one), and @active_groups is the list of all possible
 * groups ordered so that the first one is the most recently used.
 *
 * This function will create new groups, starting from group containing the
 * most "active" sta.
 * For example if the list of sta is :
 * sta8 -> sta3 -> sta4 -> sta7 -> sta1
 * and the number of user per group is 3, it will create grooups :
 * - sta8 / sta3 / sta4
 * - sta8 / sta7 / sta1
 * - sta3 / sta4 / sta7
 * - sta3 / sta1
 * - sta4 / sta7 / sta1
 * - sta7 / sta1
 *
 * To create new group, the least used group are first selected.
 * It is only allowed to create NX_MU_GROUP_MAX per iteration.
 *
 * Once groups have been updated, mu group information is update to the fw.
 * To do so it use the @update_sta list to know which sta has been affected.
 * As it is necessary to wait for fw confirmation before using this new group
 * MU is temporarily disabled during group update
 *
 * Work is then rescheduled.
 *
 * At the end of the function, both @active_sta and @update_sta list are empty.
 *
 * Note:
 * - This is still a WIP, and will require more tuning
 * - not all combinations are created, to avoid to much processing.
 * - reschedule delay should be adaptative
 */
void ecrnx_mu_group_work(struct work_struct *ws)
{
    struct delayed_work *dw = container_of(ws, struct delayed_work, work);
    struct ecrnx_mu_info *mu = container_of(dw, struct ecrnx_mu_info, group_work);
    struct ecrnx_hw *ecrnx_hw = container_of(mu, struct ecrnx_hw, mu);
    struct ecrnx_sta *sta, *next;
    int nb_group_left = NX_MU_GROUP_MAX;

    if (WARN(!ecrnx_hw->mod_params->mutx,
             "In group formation work, but mutx disabled"))
        return;

    if (down_interruptible(&mu->lock) != 0)
        return;

    mu->update_count++;
    if (!mu->update_count)
        mu->update_count++;

    list_for_each_entry_safe(sta, next, &mu->active_sta, group_info.active) {
        if (nb_group_left)
            ecrnx_mu_group_create(mu, sta, &nb_group_left);

        sta->group_info.last_update = mu->update_count;
        list_del(&sta->group_info.active);
    }

    if (! list_empty(&mu->update_sta)) {
        list_for_each_entry_safe(sta, next, &mu->update_sta, group_info.update) {
            ecrnx_send_mu_group_update_req(ecrnx_hw, sta);
            list_del(&sta->group_info.update);
        }
    }

    mu->next_group_select = jiffies;
    ecrnx_mu_group_sta_select(ecrnx_hw);
    up(&mu->lock);

    return;
}

/**
 * ecrnx_mu_group_init - Initialize MU groups
 *
 * @ecrnx_hw: main driver data
 *
 * Initialize all MU group
 */
void ecrnx_mu_group_init(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_mu_info *mu = &ecrnx_hw->mu;
    int i;

    INIT_LIST_HEAD(&mu->active_groups);
    INIT_LIST_HEAD(&mu->active_sta);
    INIT_LIST_HEAD(&mu->update_sta);

    for (i = 0; i < NX_MU_GROUP_MAX; i++) {
        int j;
        mu->groups[i].user_cnt = 0;
        mu->groups[i].group_id = i + 1;
        for (j = 0; j < CONFIG_USER_MAX; j++) {
            mu->groups[i].users[j] = NULL;
        }
        list_add(&mu->groups[i].list, &mu->active_groups);
    }

    mu->update_count = 1;
    mu->group_cnt = 0;
    mu->next_group_select = jiffies;
    INIT_DELAYED_WORK(&mu->group_work, ecrnx_mu_group_work);
    sema_init(&mu->lock, 1);
}

/**
 * ecrnx_mu_set_active_sta - mark a STA as active
 *
 * @ecrnx_hw: main driver data
 * @sta: pointer to the sta
 * @traffic: Number of buffers to add in the sta's traffic counter
 *
 * If @sta is MU beamformee capable (and MU-MIMO tx is enabled) move the
 * sta at the top of the @active_sta list.
 * It also schedule the group_work if not already scheduled and the list
 * contains more than one sta.
 *
 * If a STA was already in the list during the last group update
 * (i.e. sta->group_info.last_update == mu->update_count) it is not added
 * back to the list until a sta that wasn't active during the last update is
 * added. This is to avoid scheduling group update with a list of sta that
 * were all already in the list during previous update.
 *
 * It is called with mu->lock taken.
 */
void ecrnx_mu_set_active_sta(struct ecrnx_hw *ecrnx_hw, struct ecrnx_sta *sta,
                            int traffic)
{
    struct ecrnx_mu_info *mu = &ecrnx_hw->mu;

    if (!sta || (sta->group_info.map & ECRNX_SU_GROUP))
        return;

    sta->group_info.traffic += traffic;

    if ((sta->group_info.last_update != mu->update_count) ||
        !list_empty(&mu->active_sta)) {

        ecrnx_mu_group_move_head(&mu->active_sta, &sta->group_info.active);

        if (!delayed_work_pending(&mu->group_work) &&
            !list_is_singular(&mu->active_sta)) {
            schedule_delayed_work(&mu->group_work,
                                  msecs_to_jiffies(ECRNX_MU_GROUP_INTERVAL));
        }
    }
}

/**
 * ecrnx_mu_set_active_group - mark a MU group as active
 *
 * @ecrnx_hw: main driver data
 * @group_id: Group id
 *
 * move a group at the top of the @active_groups list
 */
void ecrnx_mu_set_active_group(struct ecrnx_hw *ecrnx_hw, int group_id)
{
    struct ecrnx_mu_info *mu = &ecrnx_hw->mu;
    struct ecrnx_mu_group *group = ecrnx_mu_group_from_id(mu, group_id);

    ecrnx_mu_group_move_head(&mu->active_groups, &group->list);
}


/**
 * ecrnx_mu_group_sta_select - Select the best group for MU stas
 *
 * @ecrnx_hw: main driver data
 *
 * For each MU capable client of AP interfaces this function tries to select
 * the best group to use.
 *
 * In first pass, gather information from all stations to form statistics
 * for each group for the previous @ECRNX_MU_GROUP_SELECT_INTERVAL interval:
 * - number of buffers transmitted
 * - number of user
 *
 * Then groups with more than 2 active users, are assigned after being ordered
 * by traffic :
 * - group with highest traffic is selected: set this group for all its users
 * - update nb_users for all others group (as one sta may be in several groups)
 * - select the next group that have still mor than 2 users and assign it.
 * - continue until all group are processed
 *
 */
void ecrnx_mu_group_sta_select(struct ecrnx_hw *ecrnx_hw)
{
    struct ecrnx_mu_info *mu = &ecrnx_hw->mu;
    int nb_users[NX_MU_GROUP_MAX + 1];
    int traffic[NX_MU_GROUP_MAX + 1];
    int order[NX_MU_GROUP_MAX + 1];
    struct ecrnx_sta *sta;
    struct ecrnx_vif *vif;
    struct list_head *head;
    u64 map;
    int i, j, update, group_id, tmp, cnt = 0;

    if (!mu->group_cnt || time_before(jiffies, mu->next_group_select))
        return;

    list_for_each_entry(vif, &ecrnx_hw->vifs, list) {

        if (ECRNX_VIF_TYPE(vif) != NL80211_IFTYPE_AP)
            continue;

#ifdef CONFIG_ECRNX_FULLMAC
        head = &vif->ap.sta_list;
#else
        head = &vif->stations;
#endif /* CONFIG_ECRNX_FULLMAC */

        memset(nb_users, 0, sizeof(nb_users));
        memset(traffic, 0, sizeof(traffic));
        list_for_each_entry(sta, head, list) {
            int sta_traffic = sta->group_info.traffic;

            /* reset statistics for next selection */
            sta->group_info.traffic = 0;
            if (sta->group_info.group)
                trace_mu_group_selection(sta, 0);
            sta->group_info.group = 0;

            if (sta->group_info.cnt == 0 ||
                sta_traffic < ECRNX_MU_GROUP_MIN_TRAFFIC)
                continue;

            group_sta_for_each(sta, group_id, map) {
                nb_users[group_id]++;
                traffic[group_id] += sta_traffic;

                /* list group with 2 users or more */
                if (nb_users[group_id] == 2)
                    order[cnt++] = group_id;
            }
        }

        /* reorder list of group with more that 2 users */
        update = 1;
        while(update) {
            update = 0;
            for (i = 0; i < cnt - 1; i++) {
                if (traffic[order[i]] < traffic[order[i + 1]]) {
                    tmp = order[i];
                    order[i] = order[i + 1];
                    order[i + 1] = tmp;
                    update = 1;
                }
            }
        }

        /* now assign group in traffic order */
        for (i = 0; i < cnt ; i ++) {
            struct ecrnx_mu_group *group;
            group_id = order[i];

            if (nb_users[group_id] < 2)
                continue;

            group = ecrnx_mu_group_from_id(mu, group_id);
            for (j = 0; j < CONFIG_USER_MAX ; j++) {
                if (group->users[j]) {
                    trace_mu_group_selection(group->users[j], group_id);
                    group->users[j]->group_info.group = group_id;

                    group_sta_for_each(group->users[j], tmp, map) {
                        if (group_id != tmp)
                            nb_users[tmp]--;
                    }
                }
            }
        }
    }

    mu->next_group_select = jiffies +
        msecs_to_jiffies(ECRNX_MU_GROUP_SELECT_INTERVAL);
    mu->next_group_select |= 1;
}
