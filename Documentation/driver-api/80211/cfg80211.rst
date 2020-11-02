==================
cfg80211 subsystem
==================

.. kernel-doc:: include/net/cfg80211.h
   :doc: Introduction

Device registration
===================

.. kernel-doc:: include/net/cfg80211.h
   :doc: Device registration

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	ieee80211_channel_flags
	ieee80211_channel
	ieee80211_rate_flags
	ieee80211_rate
	ieee80211_sta_ht_cap
	ieee80211_supported_band
	cfg80211_signal_type
	wiphy_params_flags
	wiphy_flags
	wiphy
	wireless_dev
	wiphy_new
	wiphy_read_of_freq_limits
	wiphy_register
	wiphy_unregister
	wiphy_free
	wiphy_name
	wiphy_dev
	wiphy_priv
	priv_to_wiphy
	set_wiphy_dev
	wdev_priv
	ieee80211_iface_limit
	ieee80211_iface_combination
	cfg80211_check_combinations

Actions and configuration
=========================

.. kernel-doc:: include/net/cfg80211.h
   :doc: Actions and configuration

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	cfg80211_ops
	vif_params
	key_params
	survey_info_flags
	survey_info
	cfg80211_beacon_data
	cfg80211_ap_settings
	station_parameters
	rate_info_flags
	rate_info
	station_info
	monitor_flags
	mpath_info_flags
	mpath_info
	bss_parameters
	ieee80211_txq_params
	cfg80211_crypto_settings
	cfg80211_auth_request
	cfg80211_assoc_request
	cfg80211_deauth_request
	cfg80211_disassoc_request
	cfg80211_ibss_params
	cfg80211_connect_params
	cfg80211_pmksa
	cfg80211_rx_mlme_mgmt
	cfg80211_auth_timeout
	cfg80211_rx_assoc_resp
	cfg80211_assoc_timeout
	cfg80211_tx_mlme_mgmt
	cfg80211_ibss_joined
	cfg80211_connect_resp_params
	cfg80211_connect_done
	cfg80211_connect_result
	cfg80211_connect_bss
	cfg80211_connect_timeout
	cfg80211_roamed
	cfg80211_disconnected
	cfg80211_ready_on_channel
	cfg80211_remain_on_channel_expired
	cfg80211_new_sta
	cfg80211_rx_mgmt
	cfg80211_mgmt_tx_status
	cfg80211_cqm_rssi_notify
	cfg80211_cqm_pktloss_notify
	cfg80211_michael_mic_failure

Scanning and BSS list handling
==============================

.. kernel-doc:: include/net/cfg80211.h
   :doc: Scanning and BSS list handling

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	cfg80211_ssid
	cfg80211_scan_request
	cfg80211_scan_done
	cfg80211_bss
	cfg80211_inform_bss
	cfg80211_inform_bss_frame_data
	cfg80211_inform_bss_data
	cfg80211_unlink_bss
	cfg80211_find_ie
	ieee80211_bss_get_ie

Utility functions
=================

.. kernel-doc:: include/net/cfg80211.h
   :doc: Utility functions

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	ieee80211_channel_to_frequency
	ieee80211_frequency_to_channel
	ieee80211_get_channel
	ieee80211_get_response_rate
	ieee80211_hdrlen
	ieee80211_get_hdrlen_from_skb
	ieee80211_radiotap_iterator

Data path helpers
=================

.. kernel-doc:: include/net/cfg80211.h
   :doc: Data path helpers

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	ieee80211_data_to_8023
	ieee80211_amsdu_to_8023s
	cfg80211_classify8021d

Regulatory enforcement infrastructure
=====================================

.. kernel-doc:: include/net/cfg80211.h
   :doc: Regulatory enforcement infrastructure

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	regulatory_hint
	wiphy_apply_custom_regulatory
	freq_reg_info

RFkill integration
==================

.. kernel-doc:: include/net/cfg80211.h
   :doc: RFkill integration

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	wiphy_rfkill_set_hw_state
	wiphy_rfkill_start_polling
	wiphy_rfkill_stop_polling

Test mode
=========

.. kernel-doc:: include/net/cfg80211.h
   :doc: Test mode

.. kernel-doc:: include/net/cfg80211.h
   :functions:
	cfg80211_testmode_alloc_reply_skb
	cfg80211_testmode_reply
	cfg80211_testmode_alloc_event_skb
	cfg80211_testmode_event
