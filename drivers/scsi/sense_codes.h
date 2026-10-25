/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The canonical list of T10 Additional Sense Codes is available at:
 * http://www.t10.org/lists/asc-num.txt [most recent: 20200817]
 */

SENSE_CODE(NO_ADDITIONAL_SENSE_INFORMATION,
	   "No additional sense information")
SENSE_CODE(FILEMARK_DETECTED,
	   "Filemark detected")
SENSE_CODE(END_OF_PARTITION_MEDIUM_DETECTED,
	   "End-of-partition/medium detected")
SENSE_CODE(SETMARK_DETECTED,
	   "Setmark detected")
SENSE_CODE(BEGINNING_OF_PARTITION_MEDIUM_DETECTED,
	   "Beginning-of-partition/medium detected")
SENSE_CODE(END_OF_DATA_DETECTED,
	   "End-of-data detected")
SENSE_CODE(IO_PROCESS_TERMINATED,
	   "I/O process terminated")
SENSE_CODE(PROGRAMMABLE_EARLY_WARNING_DETECTED,
	   "Programmable early warning detected")
SENSE_CODE(AUDIO_PLAY_OP_IN_PROGRESS,
	   "Audio play operation in progress")
SENSE_CODE(AUDIO_PLAY_OP_PAUSED,
	   "Audio play operation paused")
SENSE_CODE(AUDIO_PLAY_OP_SUCCESSFULLY_COMPLETED,
	   "Audio play operation successfully completed")
SENSE_CODE(AUDIO_PLAY_OP_STOPPED_DUE_TO_ERROR,
	   "Audio play operation stopped due to error")
SENSE_CODE(NO_CURRENT_AUDIO_STATUS_TO_RETURN,
	   "No current audio status to return")
SENSE_CODE(OP_IN_PROGRESS,
	   "Operation in progress")
SENSE_CODE(CLEANING_REQUESTED,
	   "Cleaning requested")
SENSE_CODE(ERASE_OP_IN_PROGRESS,
	   "Erase operation in progress")
SENSE_CODE(LOCATE_OP_IN_PROGRESS,
	   "Locate operation in progress")
SENSE_CODE(REWIND_OP_IN_PROGRESS,
	   "Rewind operation in progress")
SENSE_CODE(SET_CAPACITY_OP_IN_PROGRESS,
	   "Set capacity operation in progress")
SENSE_CODE(VERIFY_OP_IN_PROGRESS,
	   "Verify operation in progress")
SENSE_CODE(ATA_PASS_THROUGH_INFORMATION_AVAILABLE,
	   "ATA pass through information available")
SENSE_CODE(CONFLICTING_SA_CREATION_REQUEST,
	   "Conflicting SA creation request")
SENSE_CODE(LU_TRANSITIONING_TO_ANOTHER_POWER_CONDITION,
	   "Logical unit transitioning to another power condition")
SENSE_CODE(EXTENDED_COPY_INFORMATION_AVAILABLE,
	   "Extended copy information available")
SENSE_CODE(ATOMIC_COMMAND_ABORTED_DUE_TO_ACA,
	   "Atomic command aborted due to ACA")
SENSE_CODE(DEFERRED_MICROCODE_IS_PENDING,
	   "Deferred microcode is pending")
SENSE_CODE(OVERLAPPING_ATOMIC_COMMAND_IN_PROGRESS,
	   "Overlapping atomic command in progress")

SENSE_CODE(NO_INDEX_SECTOR_SIGNAL,
	   "No index/sector signal")

SENSE_CODE(NO_SEEK_COMPLETE,
	   "No seek complete")

SENSE_CODE(PERIPHERAL_DEVICE_WRITE_FAULT,
	   "Peripheral device write fault")
SENSE_CODE(NO_WRITE_CURRENT,
	   "No write current")
SENSE_CODE(EXCESSIVE_WRITE_ERRORS,
	   "Excessive write errors")

SENSE_CODE(LU_NOT_READY,
	   "Logical unit not ready, cause not reportable")
SENSE_CODE(LU_IS_IN_PROCESS_OF_BECOMING_READY,
	   "Logical unit is in process of becoming ready")
SENSE_CODE(LU_NOT_READY_INITIALIZING_COMMAND_REQUIRED,
	   "Logical unit not ready, initializing command required")
SENSE_CODE(LU_NOT_READY_MANUAL_INTERVENTION_REQUIRED,
	   "Logical unit not ready, manual intervention required")
SENSE_CODE(LU_NOT_READY_FORMAT_IN_PROGRESS,
	   "Logical unit not ready, format in progress")
SENSE_CODE(LU_NOT_READY_REBUILD_IN_PROGRESS,
	   "Logical unit not ready, rebuild in progress")
SENSE_CODE(LU_NOT_READY_RECALCULATION_IN_PROGRESS,
	   "Logical unit not ready, recalculation in progress")
SENSE_CODE(LU_NOT_READY_OP_IN_PROGRESS,
	   "Logical unit not ready, operation in progress")
SENSE_CODE(LU_NOT_READY_LONG_WRITE_IN_PROGRESS,
	   "Logical unit not ready, long write in progress")
SENSE_CODE(LU_NOT_READY_SELFTEST_IN_PROGRESS,
	   "Logical unit not ready, self-test in progress")
SENSE_CODE(LU_NOT_ACCESSIBLE_ASYMMETRIC_ACCESS_STATE_TRANSITION,
	   "Logical unit not accessible, asymmetric access state transition")
SENSE_CODE(LU_NOT_ACCESSIBLE_TARGET_PORT_IN_STANDBY_STATE,
	   "Logical unit not accessible, target port in standby state")
SENSE_CODE(LU_NOT_ACCESSIBLE_TARGET_PORT_IN_UNAVAILABLE_STATE,
	   "Logical unit not accessible, target port in unavailable state")
SENSE_CODE(LU_NOT_READY_STRUCTURE_CHECK_REQUIRED,
	   "Logical unit not ready, structure check required")
SENSE_CODE(LU_NOT_READY_SECURITY_SESSION_IN_PROGRESS,
	   "Logical unit not ready, security session in progress")
SENSE_CODE(LU_NOT_READY_AUXILIARY_MEMORY_NOT_ACCESSIBLE,
	   "Logical unit not ready, auxiliary memory not accessible")
SENSE_CODE(LU_NOT_READY_NOTIFY_REQUIRED,
	   "Logical unit not ready, notify (enable spinup) required")
SENSE_CODE(LU_NOT_READY_OFFLINE,
	   "Logical unit not ready, offline")
SENSE_CODE(LU_NOT_READY_SA_CREATION_IN_PROGRESS,
	   "Logical unit not ready, SA creation in progress")
SENSE_CODE(LU_NOT_READY_SPACE_ALLOCATION_IN_PROGRESS,
	   "Logical unit not ready, space allocation in progress")
SENSE_CODE(LU_NOT_READY_ROBOTICS_DISABLED,
	   "Logical unit not ready, robotics disabled")
SENSE_CODE(LU_NOT_READY_CONFIG_REQUIRED,
	   "Logical unit not ready, configuration required")
SENSE_CODE(LU_NOT_READY_CALIBRATION_REQUIRED,
	   "Logical unit not ready, calibration required")
SENSE_CODE(LU_NOT_READY_A_DOOR_IS_OPEN,
	   "Logical unit not ready, a door is open")
SENSE_CODE(LU_NOT_READY_OPERATING_IN_SEQUENTIAL_MODE,
	   "Logical unit not ready, operating in sequential mode")
SENSE_CODE(LU_NOT_READY_START_STOP_UNIT_COMMAND_IN_PROGRESS,
	   "Logical unit not ready, start stop unit command in progress")
SENSE_CODE(LU_NOT_READY_SANITIZE_IN_PROGRESS,
	   "Logical unit not ready, sanitize in progress")
SENSE_CODE(LU_NOT_READY_ADDITIONAL_POWER_USE_NOT_YET_GRANTED,
	   "Logical unit not ready, additional power use not yet granted")
SENSE_CODE(LU_NOT_READY_CONFIG_IN_PROGRESS,
	   "Logical unit not ready, configuration in progress")
SENSE_CODE(LU_NOT_READY_MICROCODE_ACTIVATION_REQUIRED,
	   "Logical unit not ready, microcode activation required")
SENSE_CODE(LU_NOT_READY_MICROCODE_DOWNLOAD_REQUIRED,
	   "Logical unit not ready, microcode download required")
SENSE_CODE(LU_NOT_READY_LU_RESET_REQUIRED,
	   "Logical unit not ready, logical unit reset required")
SENSE_CODE(LU_NOT_READY_HARD_RESET_REQUIRED,
	   "Logical unit not ready, hard reset required")
SENSE_CODE(LU_NOT_READY_POWER_CYCLE_REQUIRED,
	   "Logical unit not ready, power cycle required")
SENSE_CODE(LU_NOT_READY_AFFILIATION_REQUIRED,
	   "Logical unit not ready, affiliation required")
SENSE_CODE(DEPOPULATION_IN_PROGRESS,
	   "Depopulation in progress")
SENSE_CODE(DEPOPULATION_RESTORATION_IN_PROGRESS,
	   "Depopulation restoration in progress")

SENSE_CODE(LU_DOES_NOT_RESPOND_TO_SELECTION,
	   "Logical unit does not respond to selection")

SENSE_CODE(NO_REFERENCE_POSITION_FOUND,
	   "No reference position found")

SENSE_CODE(MULTIPLE_PERIPHERAL_DEVICES_SELECTED,
	   "Multiple peripheral devices selected")

SENSE_CODE(LU_COMMUNICATION_FAILURE,
	   "Logical unit communication failure")
SENSE_CODE(LU_COMMUNICATION_TIMEOUT,
	   "Logical unit communication time-out")
SENSE_CODE(LU_COMMUNICATION_PARITY_ERROR,
	   "Logical unit communication parity error")
SENSE_CODE(LU_COMMUNICATION_CRC_ERROR,
	   "Logical unit communication CRC error (Ultra-DMA/32)")
SENSE_CODE(UNREACHABLE_COPY_TARGET,
	   "Unreachable copy target")

SENSE_CODE(TRACK_FOLLOWING_ERROR,
	   "Track following error")
SENSE_CODE(TRACKING_SERVO_FAILURE,
	   "Tracking servo failure")
SENSE_CODE(FOCUS_SERVO_FAILURE,
	   "Focus servo failure")
SENSE_CODE(SPINDLE_SERVO_FAILURE,
	   "Spindle servo failure")
SENSE_CODE(HEAD_SELECT_FAULT,
	   "Head select fault")
SENSE_CODE(VIBRATION_INDUCED_TRACKING_ERROR,
	   "Vibration induced tracking error")

SENSE_CODE(ERROR_LOG_OVERFLOW,
	   "Error log overflow")

SENSE_CODE(SSC_WARNING,
	   "Warning")
SENSE_CODE(WARNING_SPECIFIED_TEMPERATURE_EXCEEDED,
	   "Warning - specified temperature exceeded")
SENSE_CODE(WARNING_ENCLOSURE_DEGRADED,
	   "Warning - enclosure degraded")
SENSE_CODE(WARNING_BACKGROUND_SELFTEST_FAILED,
	   "Warning - background self-test failed")
SENSE_CODE(WARNING_BACKGROUND_PRESCAN_DETECTED_MEDIUM_ERROR,
	   "Warning - background pre-scan detected medium error")
SENSE_CODE(WARNING_BACKGROUND_MEDIUM_SCAN_DETECTED_MEDIUM_ERROR,
	   "Warning - background medium scan detected medium error")
SENSE_CODE(WARNING_NONVOLATILE_CACHE_NOW_VOLATILE,
	   "Warning - non-volatile cache now volatile")
SENSE_CODE(WARNING_DEGRADED_POWER_TO_NONVOLATILE_CACHE,
	   "Warning - degraded power to non-volatile cache")
SENSE_CODE(WARNING_POWER_LOSS_EXPECTED,
	   "Warning - power loss expected")
SENSE_CODE(WARNING_DEVICE_STATISTICS_NOTIFICATION_ACTIVE,
	   "Warning - device statistics notification active")
SENSE_CODE(WARNING_HIGH_CRITICAL_TEMPERATURE_LIMIT_EXCEEDED,
	   "Warning - high critical temperature limit exceeded")
SENSE_CODE(WARNING_LOW_CRITICAL_TEMPERATURE_LIMIT_EXCEEDED,
	   "Warning - low critical temperature limit exceeded")
SENSE_CODE(WARNING_HIGH_OPERATING_TEMPERATURE_LIMIT_EXCEEDED,
	   "Warning - high operating temperature limit exceeded")
SENSE_CODE(WARNING_LOW_OPERATING_TEMPERATURE_LIMIT_EXCEEDED,
	   "Warning - low operating temperature limit exceeded")
SENSE_CODE(WARNING_HIGH_CRITICAL_HUMIDITY_LIMIT_EXCEEDED,
	   "Warning - high critical humidity limit exceeded")
SENSE_CODE(WARNING_LOW_CRITICAL_HUMIDITY_LIMIT_EXCEEDED,
	   "Warning - low critical humidity limit exceeded")
SENSE_CODE(WARNING_HIGH_OPERATING_HUMIDITY_LIMIT_EXCEEDED,
	   "Warning - high operating humidity limit exceeded")
SENSE_CODE(WARNING_LOW_OPERATING_HUMIDITY_LIMIT_EXCEEDED,
	   "Warning - low operating humidity limit exceeded")
SENSE_CODE(WARNING_MICROCODE_SECURITY_AT_RISK,
	   "Warning - microcode security at risk")
SENSE_CODE(WARNING_MICROCODE_DIGITAL_SIGNATURE_VALIDATION_FAILURE,
	   "Warning - microcode digital signature validation failure")
SENSE_CODE(WARNING_PHYSICAL_ELEMENT_STATUS_CHANGE,
	   "Warning - physical element status change")

SENSE_CODE(SSC_WRITE_ERROR,
	   "Write error")
SENSE_CODE(WRITE_ERROR_RECOVERED_WITH_AUTO_REALLOCATION,
	   "Write error - recovered with auto reallocation")
SENSE_CODE(WRITE_ERROR_AUTO_REALLOCATION_FAILED,
	   "Write error - auto reallocation failed")
SENSE_CODE(WRITE_ERROR_RECOMMEND_REASSIGNMENT,
	   "Write error - recommend reassignment")
SENSE_CODE(COMPRESSION_CHECK_MISCOMPARE_ERROR,
	   "Compression check miscompare error")
SENSE_CODE(DATA_EXPANSION_OCCURRED_DURING_COMPRESSION,
	   "Data expansion occurred during compression")
SENSE_CODE(BLOCK_NOT_COMPRESSIBLE,
	   "Block not compressible")
SENSE_CODE(WRITE_ERROR_RECOVERY_NEEDED,
	   "Write error - recovery needed")
SENSE_CODE(WRITE_ERROR_RECOVERY_FAILED,
	   "Write error - recovery failed")
SENSE_CODE(WRITE_ERROR_LOSS_OF_STREAMING,
	   "Write error - loss of streaming")
SENSE_CODE(WRITE_ERROR_PADDING_BLOCKS_ADDED,
	   "Write error - padding blocks added")
SENSE_CODE(AUXILIARY_MEMORY_WRITE_ERROR,
	   "Auxiliary memory write error")
SENSE_CODE(WRITE_ERROR_UNEXPECTED_UNSOLICITED_DATA,
	   "Write error - unexpected unsolicited data")
SENSE_CODE(WRITE_ERROR_NOT_ENOUGH_UNSOLICITED_DATA,
	   "Write error - not enough unsolicited data")
SENSE_CODE(MULTIPLE_WRITE_ERRORS,
	   "Multiple write errors")
SENSE_CODE(DEFECTS_IN_ERROR_WINDOW,
	   "Defects in error window")
SENSE_CODE(INCOMPLETE_MULTIPLE_ATOMIC_WRITE_OPERATIONS,
	   "Incomplete multiple atomic write operations")
SENSE_CODE(WRITE_ERROR_RECOVERY_SCAN_NEEDED,
	   "Write error - recovery scan needed")
SENSE_CODE(WRITE_ERROR_INSUFFICIENT_ZONE_RESOURCES,
	   "Write error - insufficient zone resources")

SENSE_CODE(ERROR_DETECTED_BY_THIRD_PARTY_TEMPORARY_INITIATOR,
	   "Error detected by third party temporary initiator")
SENSE_CODE(THIRD_PARTY_DEVICE_FAILURE,
	   "Third party device failure")
SENSE_CODE(COPY_TARGET_DEVICE_NOT_REACHABLE,
	   "Copy target device not reachable")
SENSE_CODE(INCORRECT_COPY_TARGET_DEVICE_TYPE,
	   "Incorrect copy target device type")
SENSE_CODE(COPY_TARGET_DEVICE_DATA_UNDERRUN,
	   "Copy target device data underrun")
SENSE_CODE(COPY_TARGET_DEVICE_DATA_OVERRUN,
	   "Copy target device data overrun")

SENSE_CODE(INVALID_INFORMATION_UNIT,
	   "Invalid information unit")
SENSE_CODE(INFORMATION_UNIT_TOO_SHORT,
	   "Information unit too short")
SENSE_CODE(INFORMATION_UNIT_TOO_LONG,
	   "Information unit too long")
SENSE_CODE(INVALID_FIELD_IN_COMMAND_INFORMATION_UNIT,
	   "Invalid field in command information unit")

SENSE_CODE(ID_CRC_OR_ECC_ERROR,
	   "Id CRC or ECC error")
SENSE_CODE(LOGICAL_BLOCK_GUARD_CHECK_FAILED,
	   "Logical block guard check failed")
SENSE_CODE(LOGICAL_BLOCK_APPLICATION_TAG_CHECK_FAILED,
	   "Logical block application tag check failed")
SENSE_CODE(LOGICAL_BLOCK_REFERENCE_TAG_CHECK_FAILED,
	   "Logical block reference tag check failed")
SENSE_CODE(LOGICAL_BLOCK_PROTECTION_ERROR_ON_RECOVER_BUFFERED_DATA,
	   "Logical block protection error on recover buffered data")
SENSE_CODE(LOGICAL_BLOCK_PROTECTION_METHOD_ERROR,
	   "Logical block protection method error")

SENSE_CODE(UNRECOVERED_READ_ERROR,
	   "Unrecovered read error")
SENSE_CODE(READ_RETRIES_EXHAUSTED,
	   "Read retries exhausted")
SENSE_CODE(ERROR_TOO_LONG_TO_CORRECT,
	   "Error too long to correct")
SENSE_CODE(MULTIPLE_READ_ERRORS,
	   "Multiple read errors")
SENSE_CODE(UNRECOVERED_READ_ERROR_AUTO_REALLOCATE_FAILED,
	   "Unrecovered read error - auto reallocate failed")
SENSE_CODE(LEC_UNCORRECTABLE_ERROR,
	   "L-EC uncorrectable error")
SENSE_CODE(CIRC_UNRECOVERED_ERROR,
	   "CIRC unrecovered error")
SENSE_CODE(DATA_RESYNCHRONIZATION_ERROR,
	   "Data re-synchronization error")
SENSE_CODE(INCOMPLETE_BLOCK_READ,
	   "Incomplete block read")
SENSE_CODE(NO_GAP_FOUND,
	   "No gap found")
SENSE_CODE(MISCORRECTED_ERROR,
	   "Miscorrected error")
SENSE_CODE(UNRECOVERED_READ_ERROR_RECOMMEND_REASSIGNMENT,
	   "Unrecovered read error - recommend reassignment")
SENSE_CODE(UNRECOVERED_READ_ERROR_RECOMMEND_REWRITE_THE_DATA,
	   "Unrecovered read error - recommend rewrite the data")
SENSE_CODE(DECOMPRESSION_CRC_ERROR,
	   "De-compression CRC error")
SENSE_CODE(CANNOT_DECOMPRESS_USING_DECLARED_ALGORITHM,
	   "Cannot decompress using declared algorithm")
SENSE_CODE(ERROR_READING_UPC_EAN_NUMBER,
	   "Error reading UPC/EAN number")
SENSE_CODE(ERROR_READING_ISRC_NUMBER,
	   "Error reading ISRC number")
SENSE_CODE(READ_ERROR_LOSS_OF_STREAMING,
	   "Read error - loss of streaming")
SENSE_CODE(AUXILIARY_MEMORY_READ_ERROR,
	   "Auxiliary memory read error")
SENSE_CODE(READ_ERROR_FAILED_RETRANSMISSION_REQUEST,
	   "Read error - failed retransmission request")
SENSE_CODE(READ_ERROR_LBA_MARKED_BAD_BY_APPLICATION_CLIENT,
	   "Read error - lba marked bad by application client")
SENSE_CODE(WRITE_AFTER_SANITIZE_REQUIRED,
	   "Write after sanitize required")

SENSE_CODE(ADDRESS_MARK_NOT_FOUND_FOR_ID_FIELD,
	   "Address mark not found for id field")

SENSE_CODE(ADDRESS_MARK_NOT_FOUND_FOR_DATA_FIELD,
	   "Address mark not found for data field")

SENSE_CODE(RECORDED_ENTITY_NOT_FOUND,
	   "Recorded entity not found")
SENSE_CODE(RECORD_NOT_FOUND,
	   "Record not found")
SENSE_CODE(FILEMARK_OR_SETMARK_NOT_FOUND,
	   "Filemark or setmark not found")
SENSE_CODE(END_OF_DATA_NOT_FOUND,
	   "End-of-data not found")
SENSE_CODE(BLOCK_SEQUENCE_ERROR,
	   "Block sequence error")
SENSE_CODE(RECORD_NOT_FOUND_RECOMMEND_REASSIGNMENT,
	   "Record not found - recommend reassignment")
SENSE_CODE(RECORD_NOT_FOUND_DATA_AUTO_REALLOCATED,
	   "Record not found - data auto-reallocated")
SENSE_CODE(LOCATE_OPERATION_FAILURE,
	   "Locate operation failure")

SENSE_CODE(RANDOM_POSITIONING_ERROR,
	   "Random positioning error")
SENSE_CODE(MECHANICAL_POSITIONING_ERROR,
	   "Mechanical positioning error")
SENSE_CODE(POSITIONING_ERROR_DETECTED_BY_READ_OF_MEDIUM,
	   "Positioning error detected by read of medium")

SENSE_CODE(DATA_SYNCHRONIZATION_MARK_ERROR,
	   "Data synchronization mark error")
SENSE_CODE(DATA_SYNC_ERROR_DATA_REWRITTEN,
	   "Data sync error - data rewritten")
SENSE_CODE(DATA_SYNC_ERROR_RECOMMEND_REWRITE,
	   "Data sync error - recommend rewrite")
SENSE_CODE(DATA_SYNC_ERROR_DATA_AUTO_REALLOCATED,
	   "Data sync error - data auto-reallocated")
SENSE_CODE(DATA_SYNC_ERROR_RECOMMEND_REASSIGNMENT,
	   "Data sync error - recommend reassignment")

SENSE_CODE(RECOVERED_DATA_WITH_NO_ERROR_CORRECTION_APPLIED,
	   "Recovered data with no error correction applied")
SENSE_CODE(RECOVERED_DATA_WITH_RETRIES,
	   "Recovered data with retries")
SENSE_CODE(RECOVERED_DATA_WITH_POSITIVE_HEAD_OFFSET,
	   "Recovered data with positive head offset")
SENSE_CODE(RECOVERED_DATA_WITH_NEGATIVE_HEAD_OFFSET,
	   "Recovered data with negative head offset")
SENSE_CODE(RECOVERED_DATA_WITH_RETRIES_AND_OR_CIRC_APPLIED,
	   "Recovered data with retries and/or circ applied")
SENSE_CODE(RECOVERED_DATA_USING_PREVIOUS_SECTOR_ID,
	   "Recovered data using previous sector id")
SENSE_CODE(RECOVERED_DATA_WITHOUT_ECC_DATA_AUTO_REALLOCATED,
	   "Recovered data without ECC - data auto-reallocated")
SENSE_CODE(RECOVERED_DATA_WITHOUT_ECC_RECOMMEND_REASSIGNMENT,
	   "Recovered data without ECC - recommend reassignment")
SENSE_CODE(RECOVERED_DATA_WITHOUT_ECC_RECOMMEND_REWRITE,
	   "Recovered data without ECC - recommend rewrite")
SENSE_CODE(RECOVERED_DATA_WITHOUT_ECC_DATA_REWRITTEN,
	   "Recovered data without ECC - data rewritten")

SENSE_CODE(RECOVERED_DATA_WITH_ERROR_CORRECTION_APPLIED,
	   "Recovered data with error correction applied")
SENSE_CODE(RECOVERED_DATA_WITH_ERROR_CORRECTION_RETRIES_APPLIED,
	   "Recovered data with error corr. & retries applied")
SENSE_CODE(RECOVERED_DATA_DATA_AUTO_REALLOCATED,
	   "Recovered data - data auto-reallocated")
SENSE_CODE(RECOVERED_DATA_WITH_CIRC,
	   "Recovered data with CIRC")
SENSE_CODE(RECOVERED_DATA_WITH_LEC,
	   "Recovered data with L-EC")
SENSE_CODE(RECOVERED_DATA_RECOMMEND_REASSIGNMENT,
	   "Recovered data - recommend reassignment")
SENSE_CODE(RECOVERED_DATA_RECOMMEND_REWRITE,
	   "Recovered data - recommend rewrite")
SENSE_CODE(RECOVERED_DATA_WITH_ECC_DATA_REWRITTEN,
	   "Recovered data with ECC - data rewritten")
SENSE_CODE(RECOVERED_DATA_WITH_LINKING,
	   "Recovered data with linking")

SENSE_CODE(DEFECT_LIST_ERROR,
	   "Defect list error")
SENSE_CODE(DEFECT_LIST_NOT_AVAILABLE,
	   "Defect list not available")
SENSE_CODE(DEFECT_LIST_ERROR_IN_PRIMARY_LIST,
	   "Defect list error in primary list")
SENSE_CODE(DEFECT_LIST_ERROR_IN_GROWN_LIST,
	   "Defect list error in grown list")

SENSE_CODE(PARAMETER_LIST_LENGTH_ERROR,
	   "Parameter list length error")

SENSE_CODE(SYNCHRONOUS_DATA_TRANSFER_ERROR,
	   "Synchronous data transfer error")

SENSE_CODE(DEFECT_LIST_NOT_FOUND,
	   "Defect list not found")
SENSE_CODE(PRIMARY_DEFECT_LIST_NOT_FOUND,
	   "Primary defect list not found")
SENSE_CODE(GROWN_DEFECT_LIST_NOT_FOUND,
	   "Grown defect list not found")

SENSE_CODE(MISCOMPARE_DURING_VERIFY_OPERATION,
	   "Miscompare during verify operation")
SENSE_CODE(MISCOMPARE_VERIFY_OF_UNMAPPED_LBA,
	   "Miscompare verify of unmapped LBA")

SENSE_CODE(RECOVERED_ID_WITH_ECC_CORRECTION,
	   "Recovered id with ECC correction")

SENSE_CODE(PARTIAL_DEFECT_LIST_TRANSFER,
	   "Partial defect list transfer")

SENSE_CODE(INVALID_COMMAND_OP_CODE,
	   "Invalid command operation code")
SENSE_CODE(ACCESS_DENIED_INITIATOR_PENDING_ENROLLED,
	   "Access denied - initiator pending-enrolled")
SENSE_CODE(ACCESS_DENIED_NO_ACCESS_RIGHTS,
	   "Access denied - no access rights")
SENSE_CODE(ACCESS_DENIED_INVALID_MGMT_ID_KEY,
	   "Access denied - invalid mgmt id key")
SENSE_CODE(ILLEGAL_COMMAND_WHILE_IN_WRITE_CAPABLE_STATE,
	   "Illegal command while in write capable state")
SENSE_CODE(0x2005,
	   "Obsolete")
SENSE_CODE(ILLEGAL_COMMAND_WHILE_IN_EXPLICIT_ADDRESS_MODE,
	   "Illegal command while in explicit address mode")
SENSE_CODE(ILLEGAL_COMMAND_WHILE_IN_IMPLICIT_ADDRESS_MODE,
	   "Illegal command while in implicit address mode")
SENSE_CODE(ACCESS_DENIED_ENROLLMENT_CONFLICT,
	   "Access denied - enrollment conflict")
SENSE_CODE(ACCESS_DENIED_INVALID_LU_IDENTIFIER,
	   "Access denied - invalid LU identifier")
SENSE_CODE(ACCESS_DENIED_INVALID_PROXY_TOKEN,
	   "Access denied - invalid proxy token")
SENSE_CODE(ACCESS_DENIED_ACL_LUN_CONFLICT,
	   "Access denied - ACL LUN conflict")
SENSE_CODE(ILLEGAL_COMMAND_WHEN_NOT_IN_APPEND_ONLY_MODE,
	   "Illegal command when not in append-only mode")
SENSE_CODE(NOT_AN_ADMINISTRATIVE_LOGICAL_UNIT,
	   "Not an administrative logical unit")
SENSE_CODE(NOT_A_SUBSIDIARY_LOGICAL_UNIT,
	   "Not a subsidiary logical unit")
SENSE_CODE(NOT_A_CONGLOMERATE_LOGICAL_UNIT,
	   "Not a conglomerate logical unit")

SENSE_CODE(LOGICAL_BLOCK_ADDRESS_OUT_OF_RANGE,
	   "Logical block address out of range")
SENSE_CODE(INVALID_ELEMENT_ADDRESS,
	   "Invalid element address")
SENSE_CODE(INVALID_ADDRESS_FOR_WRITE,
	   "Invalid address for write")
SENSE_CODE(INVALID_WRITE_CROSSING_LAYER_JUMP,
	   "Invalid write crossing layer jump")
SENSE_CODE(UNALIGNED_WRITE_COMMAND,
	   "Unaligned write command")
SENSE_CODE(WRITE_BOUNDARY_VIOLATION,
	   "Write boundary violation")
SENSE_CODE(ATTEMPT_TO_READ_INVALID_DATA,
	   "Attempt to read invalid data")
SENSE_CODE(READ_BOUNDARY_VIOLATION,
	   "Read boundary violation")
SENSE_CODE(MISALIGNED_WRITE_COMMAND,
	   "Misaligned write command")
SENSE_CODE(ATTEMPT_TO_ACCESS_GAP_ZONE,
	   "Attempt to access gap zone")

SENSE_CODE(ILLEGAL_FUNCTION,
	   "Illegal function (use 20 00, 24 00, or 26 00)")

SENSE_CODE(INVALID_TOKEN_OP,
	   "Invalid token operation, cause not reportable")
SENSE_CODE(INVALID_TOKEN_OP_UNSUPPORTED_TOKEN_TYPE,
	   "Invalid token operation, unsupported token type")
SENSE_CODE(INVALID_TOKEN_OP_REMOTE_TOKEN_USAGE_NOT_SUPPORTED,
	   "Invalid token operation, remote token usage not supported")
SENSE_CODE(INVALID_TOKEN_OP_REMOTE_ROD_TOKEN_CREATION_NOT_SUPPORTED,
	   "Invalid token operation, remote rod token creation not supported")
SENSE_CODE(INVALID_TOKEN_OP_TOKEN_UNKNOWN,
	   "Invalid token operation, token unknown")
SENSE_CODE(INVALID_TOKEN_OP_TOKEN_CORRUPT,
	   "Invalid token operation, token corrupt")
SENSE_CODE(INVALID_TOKEN_OP_TOKEN_REVOKED,
	   "Invalid token operation, token revoked")
SENSE_CODE(INVALID_TOKEN_OP_TOKEN_EXPIRED,
	   "Invalid token operation, token expired")
SENSE_CODE(INVALID_TOKEN_OP_TOKEN_CANCELLED,
	   "Invalid token operation, token cancelled")
SENSE_CODE(INVALID_TOKEN_OP_TOKEN_DELETED,
	   "Invalid token operation, token deleted")
SENSE_CODE(INVALID_TOKEN_OP_INVALID_TOKEN_LENGTH,
	   "Invalid token operation, invalid token length")

SENSE_CODE(INVALID_FIELD_IN_CDB,
	   "Invalid field in cdb")
SENSE_CODE(CDB_DECRYPTION_ERROR,
	   "CDB decryption error")
SENSE_CODE(0x2402,
	   "Obsolete")
SENSE_CODE(0x2403,
	   "Obsolete")
SENSE_CODE(SECURITY_AUDIT_VALUE_FROZEN,
	   "Security audit value frozen")
SENSE_CODE(SECURITY_WORKING_KEY_FROZEN,
	   "Security working key frozen")
SENSE_CODE(NONCE_NOT_UNIQUE,
	   "Nonce not unique")
SENSE_CODE(NONCE_TIMESTAMP_OUT_OF_RANGE,
	   "Nonce timestamp out of range")
SENSE_CODE(INVALID_XCDB,
	   "Invalid XCDB")
SENSE_CODE(INVALID_FAST_FORMAT,
	   "Invalid fast format")

SENSE_CODE(LU_NOT_SUPPORTED,
	   "Logical unit not supported")

SENSE_CODE(INVALID_FIELD_IN_PARAMETER_LIST,
	   "Invalid field in parameter list")
SENSE_CODE(PARAMETER_NOT_SUPPORTED,
	   "Parameter not supported")
SENSE_CODE(PARAMETER_VALUE_INVALID,
	   "Parameter value invalid")
SENSE_CODE(THRESHOLD_PARAMETERS_NOT_SUPPORTED,
	   "Threshold parameters not supported")
SENSE_CODE(INVALID_RELEASE_OF_PERSISTENT_RESERVATION,
	   "Invalid release of persistent reservation")
SENSE_CODE(DATA_DECRYPTION_ERROR,
	   "Data decryption error")
SENSE_CODE(TOO_MANY_TARGET_DESCRIPTORS,
	   "Too many target descriptors")
SENSE_CODE(UNSUPPORTED_TARGET_DESCRIPTOR_TYPE_CODE,
	   "Unsupported target descriptor type code")
SENSE_CODE(TOO_MANY_SEGMENT_DESCRIPTORS,
	   "Too many segment descriptors")
SENSE_CODE(UNSUPPORTED_SEGMENT_DESCRIPTOR_TYPE_CODE,
	   "Unsupported segment descriptor type code")
SENSE_CODE(UNEXPECTED_INEXACT_SEGMENT,
	   "Unexpected inexact segment")
SENSE_CODE(INLINE_DATA_LENGTH_EXCEEDED,
	   "Inline data length exceeded")
SENSE_CODE(INVALID_OP_FOR_COPY_SOURCE_OR_DESTINATION,
	   "Invalid operation for copy source or destination")
SENSE_CODE(COPY_SEGMENT_GRANULARITY_VIOLATION,
	   "Copy segment granularity violation")
SENSE_CODE(INVALID_PARAMETER_WHILE_PORT_IS_ENABLED,
	   "Invalid parameter while port is enabled")
SENSE_CODE(INVALID_DATA_OUT_BUFFER_INTEGRITY_CHECK_VALUE,
	   "Invalid data-out buffer integrity check value")
SENSE_CODE(DATA_DECRYPTION_KEY_FAIL_LIMIT_REACHED,
	   "Data decryption key fail limit reached")
SENSE_CODE(INCOMPLETE_KEY_ASSOCIATED_DATA_SET,
	   "Incomplete key-associated data set")
SENSE_CODE(VENDOR_SPECIFIC_KEY_REFERENCE_NOT_FOUND,
	   "Vendor specific key reference not found")
SENSE_CODE(APPLICATION_TAG_MODE_PAGE_IS_INVALID,
	   "Application tag mode page is invalid")
SENSE_CODE(TAPE_STREAM_MIRRORING_PREVENTED,
	   "Tape stream mirroring prevented")
SENSE_CODE(COPY_SOURCE_OR_COPY_DESTINATION_NOT_AUTHORIZED,
	   "Copy source or copy destination not authorized")
SENSE_CODE(FAST_COPY_NOT_POSSIBLE,
	   "Fast copy not possible")

SENSE_CODE(WRITE_PROTECTED,
	   "Write protected")
SENSE_CODE(HW_WRITE_PROTECTED,
	   "Hardware write protected")
SENSE_CODE(LU_SOFTWARE_WRITE_PROTECTED,
	   "Logical unit software write protected")
SENSE_CODE(ASSOCIATED_WRITE_PROTECT,
	   "Associated write protect")
SENSE_CODE(PERSISTENT_WRITE_PROTECT,
	   "Persistent write protect")
SENSE_CODE(PERMANENT_WRITE_PROTECT,
	   "Permanent write protect")
SENSE_CODE(CONDITIONAL_WRITE_PROTECT,
	   "Conditional write protect")
SENSE_CODE(SPACE_ALLOCATION_FAILED_WRITE_PROTECT,
	   "Space allocation failed write protect")
SENSE_CODE(ZONE_IS_READ_ONLY,
	   "Zone is read only")

SENSE_CODE(NOT_READY_TO_READY_CHANGE_MEDIUM_MAY_HAVE_CHANGED,
	   "Not ready to ready change, medium may have changed")
SENSE_CODE(IMPORT_OR_EXPORT_ELEMENT_ACCESSED,
	   "Import or export element accessed")
SENSE_CODE(FORMAT_LAYER_MAY_HAVE_CHANGED,
	   "Format-layer may have changed")
SENSE_CODE(IMPORT_EXPORT_ELEMENT_ACCESSED_MEDIUM_CHANGED,
	   "Import/export element accessed, medium changed")

SENSE_CODE(POWER_ON_RESET_OR_BUS_DEVICE_RESET_OCCURRED,
	   "Power on, reset, or bus device reset occurred")
SENSE_CODE(POWER_ON_OCCURRED,
	   "Power on occurred")
SENSE_CODE(SCSI_BUS_RESET_OCCURRED,
	   "Scsi bus reset occurred")
SENSE_CODE(BUS_DEVICE_RESET_FUNCTION_OCCURRED,
	   "Bus device reset function occurred")
SENSE_CODE(DEVICE_INTERNAL_RESET,
	   "Device internal reset")
SENSE_CODE(TRANSCEIVER_MODE_CHANGED_TO_SINGLE_ENDED,
	   "Transceiver mode changed to single-ended")
SENSE_CODE(TRANSCEIVER_MODE_CHANGED_TO_LVD,
	   "Transceiver mode changed to lvd")
SENSE_CODE(I_T_NEXUS_LOSS_OCCURRED,
	   "I_T nexus loss occurred")

SENSE_CODE(PARAMETERS_CHANGED,
	   "Parameters changed")
SENSE_CODE(MODE_PARAMETERS_CHANGED,
	   "Mode parameters changed")
SENSE_CODE(LOG_PARAMETERS_CHANGED,
	   "Log parameters changed")
SENSE_CODE(RESERVATIONS_PREEMPTED,
	   "Reservations preempted")
SENSE_CODE(RESERVATIONS_RELEASED,
	   "Reservations released")
SENSE_CODE(REGISTRATIONS_PREEMPTED,
	   "Registrations preempted")
SENSE_CODE(ASYMMETRIC_ACCESS_STATE_CHANGED,
	   "Asymmetric access state changed")
SENSE_CODE(IMPLICIT_ASYMMETRIC_ACCESS_STATE_TRANSITION_FAILED,
	   "Implicit asymmetric access state transition failed")
SENSE_CODE(PRIORITY_CHANGED,
	   "Priority changed")
SENSE_CODE(CAPACITY_DATA_HAS_CHANGED,
	   "Capacity data has changed")
SENSE_CODE(ERROR_HISTORY_I_T_NEXUS_CLEARED,
	   "Error history I_T nexus cleared")
SENSE_CODE(ERROR_HISTORY_SNAPSHOT_RELEASED,
	   "Error history snapshot released")
SENSE_CODE(ERROR_RECOVERY_ATTRIBUTES_HAVE_CHANGED,
	   "Error recovery attributes have changed")
SENSE_CODE(DATA_ENCRYPTION_CAPABILITIES_CHANGED,
	   "Data encryption capabilities changed")
SENSE_CODE(TIMESTAMP_CHANGED,
	   "Timestamp changed")
SENSE_CODE(DATA_ENCRYPTION_PARAMS_CHANGED_BY_ANOTHER_I_T_NEXUS,
	   "Data encryption parameters changed by another i_t nexus")
SENSE_CODE(DATA_ENCRYPTION_PARAMS_CHANGED_BY_VENDOR_SPECIFIC_EVENT,
	   "Data encryption parameters changed by vendor specific event")
SENSE_CODE(DATA_ENCRYPTION_KEY_INSTANCE_COUNTER_HAS_CHANGED,
	   "Data encryption key instance counter has changed")
SENSE_CODE(SA_CREATION_CAPABILITIES_DATA_HAS_CHANGED,
	   "SA creation capabilities data has changed")
SENSE_CODE(MEDIUM_REMOVAL_PREVENTION_PREEMPTED,
	   "Medium removal prevention preempted")
SENSE_CODE(ZONE_RESET_WRITE_POINTER_RECOMMENDED,
	   "Zone reset write pointer recommended")

SENSE_CODE(COPY_CANNOT_EXECUTE_SINCE_HOST_CANNOT_DISCONNECT,
	   "Copy cannot execute since host cannot disconnect")

SENSE_CODE(COMMAND_SEQUENCE_ERROR,
	   "Command sequence error")
SENSE_CODE(TOO_MANY_WINDOWS_SPECIFIED,
	   "Too many windows specified")
SENSE_CODE(INVALID_COMBINATION_OF_WINDOWS_SPECIFIED,
	   "Invalid combination of windows specified")
SENSE_CODE(CURRENT_PROGRAM_AREA_IS_NOT_EMPTY,
	   "Current program area is not empty")
SENSE_CODE(CURRENT_PROGRAM_AREA_IS_EMPTY,
	   "Current program area is empty")
SENSE_CODE(ILLEGAL_POWER_CONDITION_REQUEST,
	   "Illegal power condition request")
SENSE_CODE(PERSISTENT_PREVENT_CONFLICT,
	   "Persistent prevent conflict")
SENSE_CODE(PREVIOUS_BUSY_STATUS,
	   "Previous busy status")
SENSE_CODE(PREVIOUS_TASK_SET_FULL_STATUS,
	   "Previous task set full status")
SENSE_CODE(PREVIOUS_RESERVATION_CONFLICT_STATUS,
	   "Previous reservation conflict status")
SENSE_CODE(PARTITION_OR_COLLECTION_CONTAINS_USER_OBJECTS,
	   "Partition or collection contains user objects")
SENSE_CODE(NOT_RESERVED,
	   "Not reserved")
SENSE_CODE(ORWRITE_GENERATION_DOES_NOT_MATCH,
	   "Orwrite generation does not match")
SENSE_CODE(RESET_WRITE_POINTER_NOT_ALLOWED,
	   "Reset write pointer not allowed")
SENSE_CODE(ZONE_IS_OFFLINE,
	   "Zone is offline")
SENSE_CODE(STREAM_NOT_OPEN,
	   "Stream not open")
SENSE_CODE(UNWRITTEN_DATA_IN_ZONE,
	   "Unwritten data in zone")
SENSE_CODE(DESCRIPTOR_FORMAT_SENSE_DATA_REQUIRED,
	   "Descriptor format sense data required")
SENSE_CODE(ZONE_IS_INACTIVE,
	   "Zone is inactive")
SENSE_CODE(WELL_KNOWN_LU_ACCESS_REQUIRED,
	   "Well known logical unit access required")

SENSE_CODE(OVERWRITE_ERROR_ON_UPDATE_IN_PLACE,
	   "Overwrite error on update in place")

SENSE_CODE(INSUFFICIENT_TIME_FOR_OPERATION,
	   "Insufficient time for operation")
SENSE_CODE(COMMAND_TIMEOUT_BEFORE_PROCESSING,
	   "Command timeout before processing")
SENSE_CODE(COMMAND_TIMEOUT_DURING_PROCESSING,
	   "Command timeout during processing")
SENSE_CODE(COMMAND_TIMEOUT_DURING_PROCESSING_DUE_TO_ERROR_RECOVERY,
	   "Command timeout during processing due to error recovery")

SENSE_CODE(COMMANDS_CLEARED_BY_ANOTHER_INITIATOR,
	   "Commands cleared by another initiator")
SENSE_CODE(COMMANDS_CLEARED_BY_POWER_LOSS_NOTIFICATION,
	   "Commands cleared by power loss notification")
SENSE_CODE(COMMANDS_CLEARED_BY_DEVICE_SERVER,
	   "Commands cleared by device server")
SENSE_CODE(SOME_COMMANDS_CLEARED_BY_QUEUING_LAYER_EVENT,
	   "Some commands cleared by queuing layer event")

SENSE_CODE(INCOMPATIBLE_MEDIUM_INSTALLED,
	   "Incompatible medium installed")
SENSE_CODE(CANNOT_READ_MEDIUM_UNKNOWN_FORMAT,
	   "Cannot read medium - unknown format")
SENSE_CODE(CANNOT_READ_MEDIUM_INCOMPATIBLE_FORMAT,
	   "Cannot read medium - incompatible format")
SENSE_CODE(CLEANING_CARTRIDGE_INSTALLED,
	   "Cleaning cartridge installed")
SENSE_CODE(CANNOT_WRITE_MEDIUM_UNKNOWN_FORMAT,
	   "Cannot write medium - unknown format")
SENSE_CODE(CANNOT_WRITE_MEDIUM_INCOMPATIBLE_FORMAT,
	   "Cannot write medium - incompatible format")
SENSE_CODE(CANNOT_FORMAT_MEDIUM_INCOMPATIBLE_MEDIUM,
	   "Cannot format medium - incompatible medium")
SENSE_CODE(CLEANING_FAILURE,
	   "Cleaning failure")
SENSE_CODE(CANNOT_WRITE_APPLICATION_CODE_MISMATCH,
	   "Cannot write - application code mismatch")
SENSE_CODE(CURRENT_SESSION_NOT_FIXATED_FOR_APPEND,
	   "Current session not fixated for append")
SENSE_CODE(CLEANING_REQUEST_REJECTED,
	   "Cleaning request rejected")
SENSE_CODE(WORM_MEDIUM_OVERWRITE_ATTEMPTED,
	   "WORM medium - overwrite attempted")
SENSE_CODE(WORM_MEDIUM_INTEGRITY_CHECK,
	   "WORM medium - integrity check")
SENSE_CODE(MEDIUM_NOT_FORMATTED,
	   "Medium not formatted")
SENSE_CODE(INCOMPATIBLE_VOLUME_TYPE,
	   "Incompatible volume type")
SENSE_CODE(INCOMPATIBLE_VOLUME_QUALIFIER,
	   "Incompatible volume qualifier")
SENSE_CODE(CLEANING_VOLUME_EXPIRED,
	   "Cleaning volume expired")

SENSE_CODE(MEDIUM_FORMAT_CORRUPTED,
	   "Medium format corrupted")
SENSE_CODE(FORMAT_COMMAND_FAILED,
	   "Format command failed")
SENSE_CODE(ZONED_FORMATTING_FAILED_DUE_TO_SPARE_LINKING,
	   "Zoned formatting failed due to spare linking")
SENSE_CODE(SANITIZE_COMMAND_FAILED,
	   "Sanitize command failed")
SENSE_CODE(DEPOPULATION_FAILED,
	   "Depopulation failed")
SENSE_CODE(DEPOPULATION_RESTORATION_FAILED,
	   "Depopulation restoration failed")

SENSE_CODE(NO_DEFECT_SPARE_LOCATION_AVAILABLE,
	   "No defect spare location available")
SENSE_CODE(DEFECT_LIST_UPDATE_FAILURE,
	   "Defect list update failure")

SENSE_CODE(TAPE_LENGTH_ERROR,
	   "Tape length error")

SENSE_CODE(ENCLOSURE_FAILURE,
	   "Enclosure failure")

SENSE_CODE(ENCLOSURE_SERVICES_FAILURE,
	   "Enclosure services failure")
SENSE_CODE(UNSUPPORTED_ENCLOSURE_FUNCTION,
	   "Unsupported enclosure function")
SENSE_CODE(ENCLOSURE_SERVICES_UNAVAILABLE,
	   "Enclosure services unavailable")
SENSE_CODE(ENCLOSURE_SERVICES_TRANSFER_FAILURE,
	   "Enclosure services transfer failure")
SENSE_CODE(ENCLOSURE_SERVICES_TRANSFER_REFUSED,
	   "Enclosure services transfer refused")
SENSE_CODE(ENCLOSURE_SERVICES_CHECKSUM_ERROR,
	   "Enclosure services checksum error")

SENSE_CODE(RIBBON_INK_OR_TONER_FAILURE,
	   "Ribbon, ink, or toner failure")

SENSE_CODE(ROUNDED_PARAMETER,
	   "Rounded parameter")

SENSE_CODE(EVENT_STATUS_NOTIFICATION,
	   "Event status notification")
SENSE_CODE(ESN_POWER_MANAGEMENT_CLASS_EVENT,
	   "Esn - power management class event")
SENSE_CODE(ESN_MEDIA_CLASS_EVENT,
	   "Esn - media class event")
SENSE_CODE(ESN_DEVICE_BUSY_CLASS_EVENT,
	   "Esn - device busy class event")
SENSE_CODE(THIN_PROVISIONING_SOFT_THRESHOLD_REACHED,
	   "Thin Provisioning soft threshold reached")
SENSE_CODE(DEPOPULATION_INTERRUPTED,
	   "Depopulation interrupted")
SENSE_CODE(DEPOPULATION_RESTORATION_INTERRUPTED,
	   "Depopulation restoration interrupted")

SENSE_CODE(SAVING_PARAMETERS_NOT_SUPPORTED,
	   "Saving parameters not supported")

SENSE_CODE(MEDIUM_NOT_PRESENT,
	   "Medium not present")
SENSE_CODE(MEDIUM_NOT_PRESENT_TRAY_CLOSED,
	   "Medium not present - tray closed")
SENSE_CODE(MEDIUM_NOT_PRESENT_TRAY_OPEN,
	   "Medium not present - tray open")
SENSE_CODE(MEDIUM_NOT_PRESENT_LOADABLE,
	   "Medium not present - loadable")
SENSE_CODE(MEDIUM_NOT_PRESENT_MEDIUM_AUXILIARY_MEMORY_ACCESSIBLE,
	   "Medium not present - medium auxiliary memory accessible")

SENSE_CODE(SEQUENTIAL_POSITIONING_ERROR,
	   "Sequential positioning error")
SENSE_CODE(TAPE_POSITION_ERROR_AT_BEGINNING_OF_MEDIUM,
	   "Tape position error at beginning-of-medium")
SENSE_CODE(TAPE_POSITION_ERROR_AT_END_OF_MEDIUM,
	   "Tape position error at end-of-medium")
SENSE_CODE(TAPE_OR_ELECTRONIC_VERTICAL_FORMS_UNIT_NOT_READY,
	   "Tape or electronic vertical forms unit not ready")
SENSE_CODE(SLEW_FAILURE,
	   "Slew failure")
SENSE_CODE(PAPER_JAM,
	   "Paper jam")
SENSE_CODE(FAILED_TO_SENSE_TOP_OF_FORM,
	   "Failed to sense top-of-form")
SENSE_CODE(FAILED_TO_SENSE_BOTTOM_OF_FORM,
	   "Failed to sense bottom-of-form")
SENSE_CODE(REPOSITION_ERROR,
	   "Reposition error")
SENSE_CODE(READ_PAST_END_OF_MEDIUM,
	   "Read past end of medium")
SENSE_CODE(READ_PAST_BEGINNING_OF_MEDIUM,
	   "Read past beginning of medium")
SENSE_CODE(POSITION_PAST_END_OF_MEDIUM,
	   "Position past end of medium")
SENSE_CODE(POSITION_PAST_BEGINNING_OF_MEDIUM,
	   "Position past beginning of medium")
SENSE_CODE(MEDIUM_DESTINATION_ELEMENT_FULL,
	   "Medium destination element full")
SENSE_CODE(MEDIUM_SOURCE_ELEMENT_EMPTY,
	   "Medium source element empty")
SENSE_CODE(END_OF_MEDIUM_REACHED,
	   "End of medium reached")
SENSE_CODE(MEDIUM_MAGAZINE_NOT_ACCESSIBLE,
	   "Medium magazine not accessible")
SENSE_CODE(MEDIUM_MAGAZINE_REMOVED,
	   "Medium magazine removed")
SENSE_CODE(MEDIUM_MAGAZINE_INSERTED,
	   "Medium magazine inserted")
SENSE_CODE(MEDIUM_MAGAZINE_LOCKED,
	   "Medium magazine locked")
SENSE_CODE(MEDIUM_MAGAZINE_UNLOCKED,
	   "Medium magazine unlocked")
SENSE_CODE(MECHANICAL_POSITIONING_OR_CHANGER_ERROR,
	   "Mechanical positioning or changer error")
SENSE_CODE(READ_PAST_END_OF_USER_OBJECT,
	   "Read past end of user object")
SENSE_CODE(ELEMENT_DISABLED,
	   "Element disabled")
SENSE_CODE(ELEMENT_ENABLED,
	   "Element enabled")
SENSE_CODE(DATA_TRANSFER_DEVICE_REMOVED,
	   "Data transfer device removed")
SENSE_CODE(DATA_TRANSFER_DEVICE_INSERTED,
	   "Data transfer device inserted")
SENSE_CODE(TOO_MANY_LOGICAL_OBJECTS_ON_PARTITION_TO_SUPPORT_OP,
	   "Too many logical objects on partition to support operation")
SENSE_CODE(ELEMENT_STATIC_INFORMATION_CHANGED,
	   "Element static information changed")

SENSE_CODE(INVALID_BITS_IN_IDENTIFY_MESSAGE,
	   "Invalid bits in identify message")

SENSE_CODE(LU_HAS_NOT_SELF_CONFIGURED_YET,
	   "Logical unit has not self-configured yet")
SENSE_CODE(LU_FAILURE,
	   "Logical unit failure")
SENSE_CODE(TIMEOUT_ON_LOGICAL_UNIT,
	   "Timeout on logical unit")
SENSE_CODE(LU_FAILED_SELF_TEST,
	   "Logical unit failed self-test")
SENSE_CODE(LU_UNABLE_TO_UPDATE_SELFTEST_LOG,
	   "Logical unit unable to update self-test log")

SENSE_CODE(TARGET_OPERATING_CONDITIONS_HAVE_CHANGED,
	   "Target operating conditions have changed")
SENSE_CODE(MICROCODE_HAS_BEEN_CHANGED,
	   "Microcode has been changed")
SENSE_CODE(CHANGED_OPERATING_DEFINITION,
	   "Changed operating definition")
SENSE_CODE(INQUIRY_DATA_HAS_CHANGED,
	   "Inquiry data has changed")
SENSE_CODE(COMPONENT_DEVICE_ATTACHED,
	   "Component device attached")
SENSE_CODE(DEVICE_IDENTIFIER_CHANGED,
	   "Device identifier changed")
SENSE_CODE(REDUNDANCY_GROUP_CREATED_OR_MODIFIED,
	   "Redundancy group created or modified")
SENSE_CODE(REDUNDANCY_GROUP_DELETED,
	   "Redundancy group deleted")
SENSE_CODE(SPARE_CREATED_OR_MODIFIED,
	   "Spare created or modified")
SENSE_CODE(SPARE_DELETED,
	   "Spare deleted")
SENSE_CODE(VOLUME_SET_CREATED_OR_MODIFIED,
	   "Volume set created or modified")
SENSE_CODE(VOLUME_SET_DELETED,
	   "Volume set deleted")
SENSE_CODE(VOLUME_SET_DEASSIGNED,
	   "Volume set deassigned")
SENSE_CODE(VOLUME_SET_REASSIGNED,
	   "Volume set reassigned")
SENSE_CODE(REPORTED_LUNS_DATA_HAS_CHANGED,
	   "Reported luns data has changed")
SENSE_CODE(ECHO_BUFFER_OVERWRITTEN,
	   "Echo buffer overwritten")
SENSE_CODE(MEDIUM_LOADABLE,
	   "Medium loadable")
SENSE_CODE(MEDIUM_AUXILIARY_MEMORY_ACCESSIBLE,
	   "Medium auxiliary memory accessible")
SENSE_CODE(iSCSI_IP_ADDRESS_ADDED,
	   "iSCSI IP address added")
SENSE_CODE(iSCSI_IP_ADDRESS_REMOVED,
	   "iSCSI IP address removed")
SENSE_CODE(iSCSI_IP_ADDRESS_CHANGED,
	   "iSCSI IP address changed")
SENSE_CODE(INSPECT_REFERRALS_SENSE_DESCRIPTORS,
	   "Inspect referrals sense descriptors")
SENSE_CODE(MICROCODE_HAS_BEEN_CHANGED_WITHOUT_RESET,
	   "Microcode has been changed without reset")
SENSE_CODE(ZONE_TRANSITION_TO_FULL,
	   "Zone transition to full")
SENSE_CODE(BIND_COMPLETED,
	   "Bind completed")
SENSE_CODE(BIND_REDIRECTED,
	   "Bind redirected")
SENSE_CODE(SUBSIDIARY_BINDING_CHANGED,
	   "Subsidiary binding changed")

/*
 * SENSE_CODE(0x40NN, "Ram failure")
 * SENSE_CODE(0x40NN, "Diagnostic failure on component nn")
 *
 * SENSE_CODE(0x41NN, "Data path failure")
 *
 * SENSE_CODE(0x42NN, "Power-on or self-test failure")
 */

SENSE_CODE(MESSAGE_ERROR,
	   "Message error")

SENSE_CODE(INTERNAL_TARGET_FAILURE,
	   "Internal target failure")
SENSE_CODE(PERSISTENT_RESERVATION_INFORMATION_LOST,
	   "Persistent reservation information lost")
SENSE_CODE(ATA_DEVICE_FAILED_SET_FEATURES,
	   "ATA device failed set features")

SENSE_CODE(SELECT_OR_RESELECT_FAILURE,
	   "Select or reselect failure")

SENSE_CODE(UNSUCCESSFUL_SOFT_RESET,
	   "Unsuccessful soft reset")

SENSE_CODE(SCSI_PARITY_ERROR,
	   "Scsi parity error")
SENSE_CODE(DATA_PHASE_CRC_ERROR_DETECTED,
	   "Data phase CRC error detected")
SENSE_CODE(SCSI_PARITY_ERROR_DETECTED_DURING_ST_DATA_PHASE,
	   "Scsi parity error detected during st data phase")
SENSE_CODE(INFORMATION_UNIT_iuCRC_ERROR_DETECTED,
	   "Information unit iuCRC error detected")
SENSE_CODE(ASYNCHRONOUS_INFORMATION_PROTECTION_ERROR_DETECTED,
	   "Asynchronous information protection error detected")
SENSE_CODE(PROTOCOL_SERVICE_CRC_ERROR,
	   "Protocol service CRC error")
SENSE_CODE(PHY_TEST_FUNCTION_IN_PROGRESS,
	   "Phy test function in progress")
SENSE_CODE(SOME_COMMANDS_CLEARED_BY_ISCSI_PROTOCOL_EVENT,
	   "Some commands cleared by iSCSI Protocol event")

SENSE_CODE(INITIATOR_DETECTED_ERROR_MESSAGE_RECEIVED,
	   "Initiator detected error message received")

SENSE_CODE(INVALID_MESSAGE_ERROR,
	   "Invalid message error")

SENSE_CODE(COMMAND_PHASE_ERROR,
	   "Command phase error")

SENSE_CODE(DATA_PHASE_ERROR,
	   "Data phase error")
SENSE_CODE(INVALID_TARGET_PORT_TRANSFER_TAG_RECEIVED,
	   "Invalid target port transfer tag received")
SENSE_CODE(TOO_MUCH_WRITE_DATA,
	   "Too much write data")
SENSE_CODE(ACK_NAK_TIMEOUT,
	   "Ack/nak timeout")
SENSE_CODE(NAK_RECEIVED,
	   "Nak received")
SENSE_CODE(DATA_OFFSET_ERROR,
	   "Data offset error")
SENSE_CODE(INITIATOR_RESPONSE_TIMEOUT,
	   "Initiator response timeout")
SENSE_CODE(CONNECTION_LOST,
	   "Connection lost")
SENSE_CODE(DATA_IN_BUFFER_OVERFLOW_BUFFER_SIZE,
	   "Data-in buffer overflow - data buffer size")
SENSE_CODE(DATA_IN_BUFFER_OVERFLOW_BUFFER_DESCRIPTOR_AREA,
	   "Data-in buffer overflow - data buffer descriptor area")
SENSE_CODE(DATA_IN_BUFFER_ERROR,
	   "Data-in buffer error")
SENSE_CODE(DATA_OUT_BUFFER_OVERFLOW_BUFFER_SIZE,
	   "Data-out buffer overflow - data buffer size")
SENSE_CODE(DATA_OUT_BUFFER_OVERFLOW_BUFFER_DESCRIPTOR_AREA,
	   "Data-out buffer overflow - data buffer descriptor area")
SENSE_CODE(DATA_OUT_BUFFER_ERROR,
	   "Data-out buffer error")
SENSE_CODE(PCIE_FABRIC_ERROR,
	   "PCIe fabric error")
SENSE_CODE(PCIE_COMPLETION_TIMEOUT,
	   "PCIe completion timeout")
SENSE_CODE(PCIE_COMPLETER_ABORT,
	   "PCIe completer abort")
SENSE_CODE(PCIE_POISONED_TLP_RECEIVED,
	   "PCIe poisoned tlp received")
SENSE_CODE(PCIE_ECRC_CHECK_FAILED,
	   "PCIe eCRC check failed")
SENSE_CODE(PCIE_UNSUPPORTED_REQUEST,
	   "PCIe unsupported request")
SENSE_CODE(PCIE_ACS_VIOLATION,
	   "PCIe acs violation")
SENSE_CODE(PCIE_TLP_PREFIX_BLOCKED,
	   "PCIe tlp prefix blocked")

SENSE_CODE(LU_FAILED_SELF_CONFIGURATION,
	   "Logical unit failed self-configuration")

/*
 * SENSE_CODE(0x4DNN, "Tagged overlapped commands (nn = queue tag)")
 */

SENSE_CODE(OVERLAPPED_COMMANDS_ATTEMPTED,
	   "Overlapped commands attempted")

SENSE_CODE(WRITE_APPEND_ERROR,
	   "Write append error")
SENSE_CODE(WRITE_APPEND_POSITION_ERROR,
	   "Write append position error")
SENSE_CODE(POSITION_ERROR_RELATED_TO_TIMING,
	   "Position error related to timing")

SENSE_CODE(ERASE_FAILURE,
	   "Erase failure")
SENSE_CODE(ERASE_FAILURE_INCOMPLETE_ERASE_OP_DETECTED,
	   "Erase failure - incomplete erase operation detected")

SENSE_CODE(CARTRIDGE_FAULT,
	   "Cartridge fault")

SENSE_CODE(MEDIA_LOAD_OR_EJECT_FAILED,
	   "Media load or eject failed")
SENSE_CODE(UNLOAD_TAPE_FAILURE,
	   "Unload tape failure")
SENSE_CODE(MEDIUM_REMOVAL_PREVENTED,
	   "Medium removal prevented")
SENSE_CODE(MEDIUM_REMOVAL_PREVENTED_BY_DATA_TRANSFER_ELEMENT,
	   "Medium removal prevented by data transfer element")
SENSE_CODE(MEDIUM_THREAD_OR_UNTHREAD_FAILURE,
	   "Medium thread or unthread failure")
SENSE_CODE(VOLUME_IDENTIFIER_INVALID,
	   "Volume identifier invalid")
SENSE_CODE(VOLUME_IDENTIFIER_MISSING,
	   "Volume identifier missing")
SENSE_CODE(DUPLICATE_VOLUME_IDENTIFIER,
	   "Duplicate volume identifier")
SENSE_CODE(ELEMENT_STATUS_UNKNOWN,
	   "Element status unknown")
SENSE_CODE(DATA_TRANSFER_DEVICE_ERR_LOAD_FAILED,
	   "Data transfer device error - load failed")
SENSE_CODE(DATA_TRANSFER_DEVICE_ERR_UNLOAD_FAILED,
	   "Data transfer device error - unload failed")
SENSE_CODE(DATA_TRANSFER_DEVICE_ERR_UNLOAD_MISSING,
	   "Data transfer device error - unload missing")
SENSE_CODE(DATA_TRANSFER_DEVICE_ERR_EJECT_FAILED,
	   "Data transfer device error - eject failed")
SENSE_CODE(DATA_TRANSFER_DEVICE_ERR_LIBRARY_COMMUNICATION_FAILED,
	   "Data transfer device error - library communication failed")

SENSE_CODE(SCSI_TO_HOST_SYSTEM_INTERFACE_FAILURE,
	   "Scsi to host system interface failure")

SENSE_CODE(SYSTEM_RESOURCE_FAILURE,
	   "System resource failure")
SENSE_CODE(SYSTEM_BUFFER_FULL,
	   "System buffer full")
SENSE_CODE(INSUFFICIENT_RESERVATION_RESOURCES,
	   "Insufficient reservation resources")
SENSE_CODE(INSUFFICIENT_RESOURCES,
	   "Insufficient resources")
SENSE_CODE(INSUFFICIENT_REGISTRATION_RESOURCES,
	   "Insufficient registration resources")
SENSE_CODE(INSUFFICIENT_ACCESS_CONTROL_RESOURCES,
	   "Insufficient access control resources")
SENSE_CODE(AUXILIARY_MEMORY_OUT_OF_SPACE,
	   "Auxiliary memory out of space")
SENSE_CODE(QUOTA_ERROR,
	   "Quota error")
SENSE_CODE(MAXIMUM_NUMBER_OF_SUPPLEMENTAL_DECRYPTION_KEYS_EXCEEDED,
	   "Maximum number of supplemental decryption keys exceeded")
SENSE_CODE(MEDIUM_AUXILIARY_MEMORY_NOT_ACCESSIBLE,
	   "Medium auxiliary memory not accessible")
SENSE_CODE(DATA_CURRENTLY_UNAVAILABLE,
	   "Data currently unavailable")
SENSE_CODE(INSUFFICIENT_POWER_FOR_OPERATION,
	   "Insufficient power for operation")
SENSE_CODE(INSUFFICIENT_RESOURCES_TO_CREATE_ROD,
	   "Insufficient resources to create rod")
SENSE_CODE(INSUFFICIENT_RESOURCES_TO_CREATE_ROD_TOKEN,
	   "Insufficient resources to create rod token")
SENSE_CODE(INSUFFICIENT_ZONE_RESOURCES,
	   "Insufficient zone resources")
SENSE_CODE(INSUFFICIENT_ZONE_RESOURCES_TO_COMPLETE_WRITE,
	   "Insufficient zone resources to complete write")
SENSE_CODE(MAXIMUM_NUMBER_OF_STREAMS_OPEN,
	   "Maximum number of streams open")
SENSE_CODE(INSUFFICIENT_RESOURCES_TO_BIND,
	   "Insufficient resources to bind")

SENSE_CODE(UNABLE_TO_RECOVER_TABLE_OF_CONTENTS,
	   "Unable to recover table-of-contents")

SENSE_CODE(GENERATION_DOES_NOT_EXIST,
	   "Generation does not exist")

SENSE_CODE(UPDATED_BLOCK_READ,
	   "Updated block read")

SENSE_CODE(OPERATOR_REQUEST_OR_STATE_CHANGE_INPUT,
	   "Operator request or state change input")
SENSE_CODE(OPERATOR_MEDIUM_REMOVAL_REQUEST,
	   "Operator medium removal request")
SENSE_CODE(OPERATOR_SELECTED_WRITE_PROTECT,
	   "Operator selected write protect")
SENSE_CODE(OPERATOR_SELECTED_WRITE_PERMIT,
	   "Operator selected write permit")

SENSE_CODE(LOG_EXCEPTION,
	   "Log exception")
SENSE_CODE(THRESHOLD_CONDITION_MET,
	   "Threshold condition met")
SENSE_CODE(LOG_COUNTER_AT_MAXIMUM,
	   "Log counter at maximum")
SENSE_CODE(LOG_LIST_CODES_EXHAUSTED,
	   "Log list codes exhausted")

SENSE_CODE(RPL_STATUS_CHANGE,
	   "Rpl status change")
SENSE_CODE(SPINDLES_SYNCHRONIZED,
	   "Spindles synchronized")
SENSE_CODE(SPINDLES_NOT_SYNCHRONIZED,
	   "Spindles not synchronized")

SENSE_CODE(FAILURE_PREDICTION_THRESHOLD_EXCEEDED,
	   "Failure prediction threshold exceeded")
SENSE_CODE(MEDIA_FAILURE_PREDICTION_THRESHOLD_EXCEEDED,
	   "Media failure prediction threshold exceeded")
SENSE_CODE(LU_FAILURE_PREDICTION_THRESHOLD_EXCEEDED,
	   "Logical unit failure prediction threshold exceeded")
SENSE_CODE(SPARE_AREA_EXHAUSTION_PREDICTION_THRESHOLD_EXCEEDED,
	   "Spare area exhaustion prediction threshold exceeded")
SENSE_CODE(HW_IMPENDING_FAILURE_GENERAL_HARD_DRIVE_FAILURE,
	   "Hardware impending failure general hard drive failure")
SENSE_CODE(HW_IMPENDING_FAILURE_DRIVE_ERROR_RATE_TOO_HIGH,
	   "Hardware impending failure drive error rate too high")
SENSE_CODE(HW_IMPENDING_FAILURE_DATA_ERROR_RATE_TOO_HIGH,
	   "Hardware impending failure data error rate too high")
SENSE_CODE(HW_IMPENDING_FAILURE_SEEK_ERROR_RATE_TOO_HIGH,
	   "Hardware impending failure seek error rate too high")
SENSE_CODE(HW_IMPENDING_FAILURE_TOO_MANY_BLOCK_REASSIGNS,
	   "Hardware impending failure too many block reassigns")
SENSE_CODE(HW_IMPENDING_FAILURE_ACCESS_TIMES_TOO_HIGH,
	   "Hardware impending failure access times too high")
SENSE_CODE(HW_IMPENDING_FAILURE_START_UNIT_TIMES_TOO_HIGH,
	   "Hardware impending failure start unit times too high")
SENSE_CODE(HW_IMPENDING_FAILURE_CHANNEL_PARAMETRICS,
	   "Hardware impending failure channel parametrics")
SENSE_CODE(HW_IMPENDING_FAILURE_CONTROLLER_DETECTED,
	   "Hardware impending failure controller detected")
SENSE_CODE(HW_IMPENDING_FAILURE_THROUGHPUT_PERFORMANCE,
	   "Hardware impending failure throughput performance")
SENSE_CODE(HW_IMPENDING_FAILURE_SEEK_TIME_PERFORMANCE,
	   "Hardware impending failure seek time performance")
SENSE_CODE(HW_IMPENDING_FAILURE_SPINUP_RETRY_COUNT,
	   "Hardware impending failure spin-up retry count")
SENSE_CODE(HW_IMPENDING_FAILURE_DRIVE_CALIBRATION_RETRY_COUNT,
	   "Hardware impending failure drive calibration retry count")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_GENERAL_HARD_DRIVE_FAILURE,
	   "Controller impending failure general hard drive failure")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_DRIVE_ERROR_RATE_TOO_HIGH,
	   "Controller impending failure drive error rate too high")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_DATA_ERROR_RATE_TOO_HIGH,
	   "Controller impending failure data error rate too high")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_SEEK_ERROR_RATE_TOO_HIGH,
	   "Controller impending failure seek error rate too high")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_TOO_MANY_BLOCK_REASSIGNS,
	   "Controller impending failure too many block reassigns")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_ACCESS_TIMES_TOO_HIGH,
	   "Controller impending failure access times too high")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_START_UNIT_TIMES_TOO_HIGH,
	   "Controller impending failure start unit times too high")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_CHANNEL_PARAMETRICS,
	   "Controller impending failure channel parametrics")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_CONTROLLER_DETECTED,
	   "Controller impending failure controller detected")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_THROUGHPUT_PERFORMANCE,
	   "Controller impending failure throughput performance")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_SEEK_TIME_PERFORMANCE,
	   "Controller impending failure seek time performance")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_SPINUP_RETRY_COUNT,
	   "Controller impending failure spin-up retry count")
SENSE_CODE(CONTROLLER_IMPENDING_FAILURE_DRIVE_CALIBRATION_RETRY_COUNT,
	   "Controller impending failure drive calibration retry count")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_GENERAL_HARD_DRIVE_FAILURE,
	   "Data channel impending failure general hard drive failure")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_DRIVE_ERROR_RATE_TOO_HIGH,
	   "Data channel impending failure drive error rate too high")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_DATA_ERROR_RATE_TOO_HIGH,
	   "Data channel impending failure data error rate too high")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_SEEK_ERROR_RATE_TOO_HIGH,
	   "Data channel impending failure seek error rate too high")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_TOO_MANY_BLOCK_REASSIGNS,
	   "Data channel impending failure too many block reassigns")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_ACCESS_TIMES_TOO_HIGH,
	   "Data channel impending failure access times too high")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_START_UNIT_TIMES_TOO_HIGH,
	   "Data channel impending failure start unit times too high")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_CHANNEL_PARAMETRICS,
	   "Data channel impending failure channel parametrics")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_CONTROLLER_DETECTED,
	   "Data channel impending failure controller detected")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_THROUGHPUT_PERFORMANCE,
	   "Data channel impending failure throughput performance")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_SEEK_TIME_PERFORMANCE,
	   "Data channel impending failure seek time performance")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_SPINUP_RETRY_COUNT,
	   "Data channel impending failure spin-up retry count")
SENSE_CODE(DATA_CHANNEL_IMPENDING_FAILURE_DRIVE_CALIBRATION_RETRY_COUNT,
	   "Data channel impending failure drive calibration retry count")
SENSE_CODE(SERVO_IMPENDING_FAILURE_GENERAL_HARD_DRIVE_FAILURE,
	   "Servo impending failure general hard drive failure")
SENSE_CODE(SERVO_IMPENDING_FAILURE_DRIVE_ERROR_RATE_TOO_HIGH,
	   "Servo impending failure drive error rate too high")
SENSE_CODE(SERVO_IMPENDING_FAILURE_DATA_ERROR_RATE_TOO_HIGH,
	   "Servo impending failure data error rate too high")
SENSE_CODE(SERVO_IMPENDING_FAILURE_SEEK_ERROR_RATE_TOO_HIGH,
	   "Servo impending failure seek error rate too high")
SENSE_CODE(SERVO_IMPENDING_FAILURE_TOO_MANY_BLOCK_REASSIGNS,
	   "Servo impending failure too many block reassigns")
SENSE_CODE(SERVO_IMPENDING_FAILURE_ACCESS_TIMES_TOO_HIGH,
	   "Servo impending failure access times too high")
SENSE_CODE(SERVO_IMPENDING_FAILURE_START_UNIT_TIMES_TOO_HIGH,
	   "Servo impending failure start unit times too high")
SENSE_CODE(SERVO_IMPENDING_FAILURE_CHANNEL_PARAMETRICS,
	   "Servo impending failure channel parametrics")
SENSE_CODE(SERVO_IMPENDING_FAILURE_CONTROLLER_DETECTED,
	   "Servo impending failure controller detected")
SENSE_CODE(SERVO_IMPENDING_FAILURE_THROUGHPUT_PERFORMANCE,
	   "Servo impending failure throughput performance")
SENSE_CODE(SERVO_IMPENDING_FAILURE_SEEK_TIME_PERFORMANCE,
	   "Servo impending failure seek time performance")
SENSE_CODE(SERVO_IMPENDING_FAILURE_SPINUP_RETRY_COUNT,
	   "Servo impending failure spin-up retry count")
SENSE_CODE(SERVO_IMPENDING_FAILURE_DRIVE_CALIBRATION_RETRY_COUNT,
	   "Servo impending failure drive calibration retry count")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_GENERAL_HARD_DRIVE_FAILURE,
	   "Spindle impending failure general hard drive failure")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_DRIVE_ERROR_RATE_TOO_HIGH,
	   "Spindle impending failure drive error rate too high")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_DATA_ERROR_RATE_TOO_HIGH,
	   "Spindle impending failure data error rate too high")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_SEEK_ERROR_RATE_TOO_HIGH,
	   "Spindle impending failure seek error rate too high")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_TOO_MANY_BLOCK_REASSIGNS,
	   "Spindle impending failure too many block reassigns")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_ACCESS_TIMES_TOO_HIGH,
	   "Spindle impending failure access times too high")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_START_UNIT_TIMES_TOO_HIGH,
	   "Spindle impending failure start unit times too high")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_CHANNEL_PARAMETRICS,
	   "Spindle impending failure channel parametrics")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_CONTROLLER_DETECTED,
	   "Spindle impending failure controller detected")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_THROUGHPUT_PERFORMANCE,
	   "Spindle impending failure throughput performance")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_SEEK_TIME_PERFORMANCE,
	   "Spindle impending failure seek time performance")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_SPINUP_RETRY_COUNT,
	   "Spindle impending failure spin-up retry count")
SENSE_CODE(SPINDLE_IMPENDING_FAILURE_DRIVE_CALIBRATION_RETRY_COUNT,
	   "Spindle impending failure drive calibration retry count")
SENSE_CODE(FW_IMPENDING_FAILURE_GENERAL_HARD_DRIVE_FAILURE,
	   "Firmware impending failure general hard drive failure")
SENSE_CODE(FW_IMPENDING_FAILURE_DRIVE_ERROR_RATE_TOO_HIGH,
	   "Firmware impending failure drive error rate too high")
SENSE_CODE(FW_IMPENDING_FAILURE_DATA_ERROR_RATE_TOO_HIGH,
	   "Firmware impending failure data error rate too high")
SENSE_CODE(FW_IMPENDING_FAILURE_SEEK_ERROR_RATE_TOO_HIGH,
	   "Firmware impending failure seek error rate too high")
SENSE_CODE(FW_IMPENDING_FAILURE_TOO_MANY_BLOCK_REASSIGNS,
	   "Firmware impending failure too many block reassigns")
SENSE_CODE(FW_IMPENDING_FAILURE_ACCESS_TIMES_TOO_HIGH,
	   "Firmware impending failure access times too high")
SENSE_CODE(FW_IMPENDING_FAILURE_START_UNIT_TIMES_TOO_HIGH,
	   "Firmware impending failure start unit times too high")
SENSE_CODE(FW_IMPENDING_FAILURE_CHANNEL_PARAMETRICS,
	   "Firmware impending failure channel parametrics")
SENSE_CODE(FW_IMPENDING_FAILURE_CONTROLLER_DETECTED,
	   "Firmware impending failure controller detected")
SENSE_CODE(FW_IMPENDING_FAILURE_THROUGHPUT_PERFORMANCE,
	   "Firmware impending failure throughput performance")
SENSE_CODE(FW_IMPENDING_FAILURE_SEEK_TIME_PERFORMANCE,
	   "Firmware impending failure seek time performance")
SENSE_CODE(FW_IMPENDING_FAILURE_SPINUP_RETRY_COUNT,
	   "Firmware impending failure spin-up retry count")
SENSE_CODE(FW_IMPENDING_FAILURE_DRIVE_CALIBRATION_RETRY_COUNT,
	   "Firmware impending failure drive calibration retry count")
SENSE_CODE(MEDIA_IMPENDING_FAILURE_ENDURANCE_LIMIT_MET,
	   "Media impending failure endurance limit met")
SENSE_CODE(FAILURE_PREDICTION_THRESHOLD_EXCEEDED_FALSE,
	   "Failure prediction threshold exceeded (false)")

SENSE_CODE(LOW_POWER_CONDITION_ON,
	   "Low power condition on")
SENSE_CODE(IDLE_CONDITION_ACTIVATED_BY_TIMER,
	   "Idle condition activated by timer")
SENSE_CODE(STANDBY_CONDITION_ACTIVATED_BY_TIMER,
	   "Standby condition activated by timer")
SENSE_CODE(IDLE_CONDITION_ACTIVATED_BY_COMMAND,
	   "Idle condition activated by command")
SENSE_CODE(STANDBY_CONDITION_ACTIVATED_BY_COMMAND,
	   "Standby condition activated by command")
SENSE_CODE(IDLE_B_CONDITION_ACTIVATED_BY_TIMER,
	   "Idle_b condition activated by timer")
SENSE_CODE(IDLE_B_CONDITION_ACTIVATED_BY_COMMAND,
	   "Idle_b condition activated by command")
SENSE_CODE(IDLE_C_CONDITION_ACTIVATED_BY_TIMER,
	   "Idle_c condition activated by timer")
SENSE_CODE(IDLE_C_CONDITION_ACTIVATED_BY_COMMAND,
	   "Idle_c condition activated by command")
SENSE_CODE(STANDBY_Y_CONDITION_ACTIVATED_BY_TIMER,
	   "Standby_y condition activated by timer")
SENSE_CODE(STANDBY_Y_CONDITION_ACTIVATED_BY_COMMAND,
	   "Standby_y condition activated by command")
SENSE_CODE(POWER_STATE_CHANGE_TO_ACTIVE,
	   "Power state change to active")
SENSE_CODE(POWER_STATE_CHANGE_TO_IDLE,
	   "Power state change to idle")
SENSE_CODE(POWER_STATE_CHANGE_TO_STANDBY,
	   "Power state change to standby")
SENSE_CODE(POWER_STATE_CHANGE_TO_SLEEP,
	   "Power state change to sleep")
SENSE_CODE(POWER_STATE_CHANGE_TO_DEVICE_CONTROL,
	   "Power state change to device control")

SENSE_CODE(LAMP_FAILURE,
	   "Lamp failure")

SENSE_CODE(VIDEO_ACQUISITION_ERROR,
	   "Video acquisition error")
SENSE_CODE(UNABLE_TO_ACQUIRE_VIDEO,
	   "Unable to acquire video")
SENSE_CODE(OUT_OF_FOCUS,
	   "Out of focus")

SENSE_CODE(SCAN_HEAD_POSITIONING_ERROR,
	   "Scan head positioning error")

SENSE_CODE(END_OF_USER_AREA_ENCOUNTERED_ON_THIS_TRACK,
	   "End of user area encountered on this track")
SENSE_CODE(PACKET_DOES_NOT_FIT_IN_AVAILABLE_SPACE,
	   "Packet does not fit in available space")

SENSE_CODE(ILLEGAL_MODE_FOR_THIS_TRACK,
	   "Illegal mode for this track")
SENSE_CODE(INVALID_PACKET_SIZE,
	   "Invalid packet size")

SENSE_CODE(VOLTAGE_FAULT,
	   "Voltage fault")

SENSE_CODE(AUTOMATIC_DOCUMENT_FEEDER_COVER_UP,
	   "Automatic document feeder cover up")
SENSE_CODE(AUTOMATIC_DOCUMENT_FEEDER_LIFT_UP,
	   "Automatic document feeder lift up")
SENSE_CODE(DOCUMENT_JAM_IN_AUTOMATIC_DOCUMENT_FEEDER,
	   "Document jam in automatic document feeder")
SENSE_CODE(DOCUMENT_MISS_FEED_AUTOMATIC_IN_DOCUMENT_FEEDER,
	   "Document miss feed automatic in document feeder")

SENSE_CODE(CONFIG_FAILURE,
	   "Configuration failure")
SENSE_CODE(CONFIG_OF_INCAPABLE_LOGICAL_UNITS_FAILED,
	   "Configuration of incapable logical units failed")
SENSE_CODE(ADD_LU_FAILED,
	   "Add logical unit failed")
SENSE_CODE(MODIFICATION_OF_LU_FAILED,
	   "Modification of logical unit failed")
SENSE_CODE(EXCHANGE_OF_LU_FAILED,
	   "Exchange of logical unit failed")
SENSE_CODE(REMOVE_OF_LU_FAILED,
	   "Remove of logical unit failed")
SENSE_CODE(ATTACHMENT_OF_LU_FAILED,
	   "Attachment of logical unit failed")
SENSE_CODE(CREATION_OF_LU_FAILED,
	   "Creation of logical unit failed")
SENSE_CODE(ASSIGN_FAILURE_OCCURRED,
	   "Assign failure occurred")
SENSE_CODE(MULTIPLY_ASSIGNED_LOGICAL_UNIT,
	   "Multiply assigned logical unit")
SENSE_CODE(SET_TARGET_PORT_GROUPS_COMMAND_FAILED,
	   "Set target port groups command failed")
SENSE_CODE(ATA_DEVICE_FEATURE_NOT_ENABLED,
	   "ATA device feature not enabled")
SENSE_CODE(COMMAND_REJECTED,
	   "Command rejected")
SENSE_CODE(EXPLICIT_BIND_NOT_ALLOWED,
	   "Explicit bind not allowed")

SENSE_CODE(LU_NOT_CONFIGURED,
	   "Logical unit not configured")
SENSE_CODE(SUBSIDIARY_LU_NOT_CONFIGURED,
	   "Subsidiary logical unit not configured")

SENSE_CODE(DATA_LOSS_ON_LOGICAL_UNIT,
	   "Data loss on logical unit")
SENSE_CODE(MULTIPLE_LU_FAILURES,
	   "Multiple logical unit failures")
SENSE_CODE(PARITY_DATA_MISMATCH,
	   "Parity/data mismatch")

SENSE_CODE(INFORMATIONAL_REFER_TO_LOG,
	   "Informational, refer to log")

SENSE_CODE(STATE_CHANGE_HAS_OCCURRED,
	   "State change has occurred")
SENSE_CODE(REDUNDANCY_LEVEL_GOT_BETTER,
	   "Redundancy level got better")
SENSE_CODE(REDUNDANCY_LEVEL_GOT_WORSE,
	   "Redundancy level got worse")

SENSE_CODE(REBUILD_FAILURE_OCCURRED,
	   "Rebuild failure occurred")

SENSE_CODE(RECALCULATE_FAILURE_OCCURRED,
	   "Recalculate failure occurred")

SENSE_CODE(COMMAND_TO_LU_FAILED,
	   "Command to logical unit failed")

SENSE_CODE(COPY_PROTECTION_KEY_EXCHANGE_FAILURE_AUTHENTICATION_FAILURE,
	   "Copy protection key exchange failure - authentication failure")
SENSE_CODE(COPY_PROTECTION_KEY_EXCHANGE_FAILURE_KEY_NOT_PRESENT,
	   "Copy protection key exchange failure - key not present")
SENSE_CODE(COPY_PROTECTION_KEY_EXCHANGE_FAILURE_KEY_NOT_ESTABLISHED,
	   "Copy protection key exchange failure - key not established")
SENSE_CODE(READ_OF_SCRAMBLED_SECTOR_WITHOUT_AUTHENTICATION,
	   "Read of scrambled sector without authentication")
SENSE_CODE(MEDIA_REGION_CODE_IS_MISMATCHED_TO_LU_REGION,
	   "Media region code is mismatched to logical unit region")
SENSE_CODE(DRIVE_REGION_MUST_BE_PERMANENT_REGION_RESET_COUNT_ERROR,
	   "Drive region must be permanent/region reset count error")
SENSE_CODE(INSUFFICIENT_BLOCK_COUNT_FOR_BINDING_NONCE_RECORDING,
	   "Insufficient block count for binding nonce recording")
SENSE_CODE(CONFLICT_IN_BINDING_NONCE_RECORDING,
	   "Conflict in binding nonce recording")
SENSE_CODE(INSUFFICIENT_PERMISSION,
	   "Insufficient permission")
SENSE_CODE(INVALID_DRIVE_HOST_PAIRING_SERVER,
	   "Invalid drive-host pairing server")
SENSE_CODE(DRIVE_HOST_PAIRING_SUSPENDED,
	   "Drive-host pairing suspended")

/*
 * SENSE_CODE(0x70NN, "Decompression exception short algorithm id of nn")
 */

SENSE_CODE(DECOMPRESSION_EXCEPTION_LONG_ALGORITHM_ID,
	   "Decompression exception long algorithm id")

SENSE_CODE(SESSION_FIXATION_ERROR,
	   "Session fixation error")
SENSE_CODE(SESSION_FIXATION_ERROR_WRITING_LEAD_IN,
	   "Session fixation error writing lead-in")
SENSE_CODE(SESSION_FIXATION_ERROR_WRITING_LEAD_OUT,
	   "Session fixation error writing lead-out")
SENSE_CODE(SESSION_FIXATION_ERROR_INCOMPLETE_TRACK_IN_SESSION,
	   "Session fixation error - incomplete track in session")
SENSE_CODE(EMPTY_OR_PARTIALLY_WRITTEN_RESERVED_TRACK,
	   "Empty or partially written reserved track")
SENSE_CODE(NO_MORE_TRACK_RESERVATIONS_ALLOWED,
	   "No more track reservations allowed")
SENSE_CODE(RMZ_EXTENSION_IS_NOT_ALLOWED,
	   "RMZ extension is not allowed")
SENSE_CODE(NO_MORE_TEST_ZONE_EXTENSIONS_ARE_ALLOWED,
	   "No more test zone extensions are allowed")

SENSE_CODE(CD_CONTROL_ERROR,
	   "Cd control error")
SENSE_CODE(POWER_CALIBRATION_AREA_ALMOST_FULL,
	   "Power calibration area almost full")
SENSE_CODE(POWER_CALIBRATION_AREA_IS_FULL,
	   "Power calibration area is full")
SENSE_CODE(POWER_CALIBRATION_AREA_ERROR,
	   "Power calibration area error")
SENSE_CODE(PROGRAM_MEMORY_AREA_UPDATE_FAILURE,
	   "Program memory area update failure")
SENSE_CODE(PROGRAM_MEMORY_AREA_IS_FULL,
	   "Program memory area is full")
SENSE_CODE(RMA_PMA_IS_ALMOST_FULL,
	   "RMA/PMA is almost full")
SENSE_CODE(CURRENT_POWER_CALIBRATION_AREA_ALMOST_FULL,
	   "Current power calibration area almost full")
SENSE_CODE(CURRENT_POWER_CALIBRATION_AREA_IS_FULL,
	   "Current power calibration area is full")
SENSE_CODE(RDZ_IS_FULL,
	   "RDZ is full")

SENSE_CODE(SECURITY_ERROR,
	   "Security error")
SENSE_CODE(UNABLE_TO_DECRYPT_DATA,
	   "Unable to decrypt data")
SENSE_CODE(UNENCRYPTED_DATA_ENCOUNTERED_WHILE_DECRYPTING,
	   "Unencrypted data encountered while decrypting")
SENSE_CODE(INCORRECT_DATA_ENCRYPTION_KEY,
	   "Incorrect data encryption key")
SENSE_CODE(CRYPTOGRAPHIC_INTEGRITY_VALIDATION_FAILED,
	   "Cryptographic integrity validation failed")
SENSE_CODE(ERROR_DECRYPTING_DATA,
	   "Error decrypting data")
SENSE_CODE(UNKNOWN_SIGNATURE_VERIFICATION_KEY,
	   "Unknown signature verification key")
SENSE_CODE(ENCRYPTION_PARAMS_NOT_USEABLE,
	   "Encryption parameters not useable")
SENSE_CODE(DIGITAL_SIGNATURE_VALIDATION_FAILURE,
	   "Digital signature validation failure")
SENSE_CODE(ENCRYPTION_MODE_MISMATCH_ON_READ,
	   "Encryption mode mismatch on read")
SENSE_CODE(ENCRYPTED_BLOCK_NOT_RAW_READ_ENABLED,
	   "Encrypted block not raw read enabled")
SENSE_CODE(INCORRECT_ENCRYPTION_PARAMS,
	   "Incorrect Encryption parameters")
SENSE_CODE(UNABLE_TO_DECRYPT_PARAMETER_LIST,
	   "Unable to decrypt parameter list")
SENSE_CODE(ENCRYPTION_ALGORITHM_DISABLED,
	   "Encryption algorithm disabled")
SENSE_CODE(SA_CREATION_PARAMETER_VALUE_INVALID,
	   "SA creation parameter value invalid")
SENSE_CODE(SA_CREATION_PARAMETER_VALUE_REJECTED,
	   "SA creation parameter value rejected")
SENSE_CODE(INVALID_SA_USAGE,
	   "Invalid SA usage")
SENSE_CODE(DATA_ENCRYPTION_CONFIG_PREVENTED,
	   "Data Encryption configuration prevented")
SENSE_CODE(SA_CREATION_PARAMETER_NOT_SUPPORTED,
	   "SA creation parameter not supported")
SENSE_CODE(AUTHENTICATION_FAILED,
	   "Authentication failed")
SENSE_CODE(EXTERNAL_DATA_ENCRYPTION_KEY_MANAGER_ACCESS_ERROR,
	   "External data encryption key manager access error")
SENSE_CODE(EXTERNAL_DATA_ENCRYPTION_KEY_MANAGER_ERROR,
	   "External data encryption key manager error")
SENSE_CODE(EXTERNAL_DATA_ENCRYPTION_KEY_NOT_FOUND,
	   "External data encryption key not found")
SENSE_CODE(EXTERNAL_DATA_ENCRYPTION_REQUEST_NOT_AUTHORIZED,
	   "External data encryption request not authorized")
SENSE_CODE(EXTERNAL_DATA_ENCRYPTION_CONTROL_TIMEOUT,
	   "External data encryption control timeout")
SENSE_CODE(EXTERNAL_DATA_ENCRYPTION_CONTROL_ERROR,
	   "External data encryption control error")
SENSE_CODE(LU_ACCESS_NOT_AUTHORIZED,
	   "Logical unit access not authorized")
SENSE_CODE(SECURITY_CONFLICT_IN_TRANSLATED_DEVICE,
	   "Security conflict in translated device")
