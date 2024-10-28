#ifndef _ETHOSN_FIRMWARE_H_
#define _ETHOSN_FIRMWARE_H_

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif

#include "uapi/ethosn_shared.h"

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdint.h>
#endif

/**
 * 这适用于 fat 二进制文件 (ethosn.bin) 和个别固件二进制文件 (fat 二进制文件的子组件).
 */
#define ETHOSN_FIRMWARE_VERSION_MAJOR 15
#define ETHOSN_FIRMWARE_VERSION_MINOR 0
#define ETHOSN_FIRMWARE_VERSION_PATCH 0

/** 缓存行的最大长度. 用于分隔主机和 Ethos-N 数据. */
#define ETHOSN_CACHE_LINE_SIZE 128

#pragma pack(push, 1)

/******************************************************************************
 * Mailbox
 ******************************************************************************/

/**
 * NPU设备中内存的地址
 * 指向 当前NPU中可由CPU直接访问的内存 的地址应为 32 位.
 * 指向 当前NPU中借助DMA复制访问的内存 的地址可达 49 位.
 */
typedef uint64_t ethosn_address_t;

/**
 * struct ethosn_queue - 动态大小的队列, 设计用于一个 CPU 写入数据, 另一个 CPU 读取数据.
 * @var capacity: 队列的总容量 (以字节为单位).
 * @var read: 队列中的读取索引 (字节). 当读取数据时, "读取" CPU 会更新这个索引.
 *             读取索引与写入索引相等时, 表示队列已空, 需要等待 "写入" CPU 写入更多数据.
 * @var write: 队列中的写入索引 (字节). 当写入数据时, "写入" CPU 会更新这个索引.
 *             写入索引与读取索引相等时, 表示队列已满, 需要等待 "读取" CPU 清空一些空间.
 * @var data: 用于存储队列数据的数组.
 *
 * 队列为空的条件是 read 索引和 write 索引相等.
 * 需要注意的是, 队列永远不会被填满到容量的极限, 以确保空和满的状态可以明确区分.
 */
struct ethosn_queue {
	union {
		struct {
			uint32_t capacity;
			uint32_t read;
		};

		/* 添加填充以避免非一致 CPU 之间的缓存问题. */
		uint8_t pad_0[ETHOSN_CACHE_LINE_SIZE];
	};
	union {
		uint32_t write;

		/* 添加填充以避免非一致 CPU 之间的缓存问题. */
		uint8_t pad_1[ETHOSN_CACHE_LINE_SIZE];
	};
	uint8_t data[];
};

/**
 * 检查给定大小的数据是否能被队列容纳, 即使队列是完全空的. 这是一个简单的容量检查, 但用函数封装是因为所涉及的比较可能与您的直觉不同.
 */
static inline bool ethosn_queue_can_ever_fit(const struct ethosn_queue *queue, uint32_t size)
{
	/* 注意我们不允许队列变得完全满, 因为那会使得满的状态与空的状态无法区分! */
	return size < queue->capacity;
}

/**
 * 获取给定队列的当前大小, 即可读的字节数.
 */
static inline uint32_t ethosn_queue_get_size(const struct ethosn_queue *queue)
{
	const uint32_t mask = queue->capacity - 1;
	return (queue->write - queue->read) & mask;
}

/**
 * 获取给定队列中的可用空间, 即可以写入的字节数.
 */
static inline uint32_t ethosn_queue_get_free_space(const struct ethosn_queue *queue)
{
	/* 注意我们减去一个以防止队列变得完全满, 因为那将无法与完全空区分开! */
	return queue->capacity - ethosn_queue_get_size(queue) - 1;
}

/**
 * 从队列中跳过给定数量的字节. 这等同于读取这些字节并丢弃它们.
 * 如果队列中的数据不足以跳过, 则返回 false.
 */
static inline bool ethosn_queue_skip(struct ethosn_queue *queue, uint8_t size)
{
	const uint32_t mask = queue->capacity - 1;
	/* 检查我们是否有足够的数据可读 */
	if (size > ethosn_queue_get_size(queue))
		return false;

	queue->read = (queue->read + size) & mask;
	return true;
}

/**
 * 从队列中读取给定数量的字节.
 * 如果队列中的数据不足以读取, 则返回 false.
 */
static inline bool ethosn_queue_read(struct ethosn_queue *queue, uint8_t *dst, uint32_t size)
{
	const uint32_t mask = queue->capacity - 1;
	uint32_t read = queue->read;
	uint32_t i;

	/* 检查我们是否有足够的数据可读 */
	if (size > ethosn_queue_get_size(queue))
		return false;

	for (i = 0; i < size; ++i) {
		dst[i] = queue->data[read];
		read = (read + 1) & mask;
	}

	queue->read = read;
	return true;
}

/**
 * 将给定的字节缓冲区写入队列.
 * 调用者需确保在确认负载对 "读取" CPU 可读 (例如刷新) 后提交 out_write_pending 指针至 queue->write.
 * 如果队列中没有足够的空闲空间, 则返回 false.
 * @buffers: 缓冲区数组, 长度为 num_buffers, 每个元素是指向将写入队列的缓冲区的指针.
 * @sizes: 数组, 长度为 num_buffers, 每个元素是 @buffers 中相应缓冲区的长度.
 */
static inline bool ethosn_queue_write(struct ethosn_queue *queue, const uint8_t *const *buffers, const uint32_t *sizes, uint32_t num_buffers,
				      uint32_t *out_write_pending)
{
	const uint32_t mask = queue->capacity - 1;
	uint32_t write = queue->write;
	uint32_t i, j;
	uint32_t total_bytes = 0;

	/* 检查我们是否有足够的空间容纳我们的数据 */
	for (i = 0; i < num_buffers; ++i)
		total_bytes += sizes[i];

	if (ethosn_queue_get_free_space(queue) < total_bytes)
		return false;

	/* 依次写入每个缓冲区 */
	for (i = 0; i < num_buffers; ++i) {
		for (j = 0; j < sizes[i]; ++j) {
			queue->data[write] = buffers[i][j];
			write = (write + 1) & mask;
		}
	}

	*out_write_pending = write;
	return true;
}

/**
 * struct ethosn_mailbox - 邮箱结构
 * @var request: 指向从主机到 Ethos-N 的消息队列的指针.
 * @var response: 指向从 Ethos-N 到主机的消息队列的指针.
 * @var severity: 日志严重级别. @see ethosn_log_severity.
 *
 * 这是主机和 Ethos-N 之间的接口.
 */
struct ethosn_mailbox {
	ethosn_address_t request;
	ethosn_address_t response;
	uint32_t severity;
};

/**
 * struct ethosn_debug_monitor_channel - 双向调试监控通信渠道.
 * @var request: 指向从主机到 Ethos-N 的消息队列的指针.
 * @var response: 指向从 Ethos-N 到主机的消息队列的指针.
 */
struct ethosn_debug_monitor_channel {
	ethosn_address_t request;
	ethosn_address_t response;
};

/******************************************************************************
 * 消息类型
 ******************************************************************************/

/**
 * 消息类型.
 */
enum ethosn_message_type {
	/* 消息类型的顺序对于增强的 RTL 测试系统很重要. 推理请求和响应不得更改, 以避免问题. */

	/* ethosn_message_inference_request */
	ETHOSN_MESSAGE_INFERENCE_REQUEST,
	/* ethosn_message_inference_response */
	ETHOSN_MESSAGE_INFERENCE_RESPONSE,

	/* ethosn_message_text */
	ETHOSN_MESSAGE_TEXT,

	/* ethosn_firmware_profiling_configuration */
	ETHOSN_MESSAGE_CONFIGURE_PROFILING,
	/* ethosn_firmware_profiling_configuration_ack */
	ETHOSN_MESSAGE_CONFIGURE_PROFILING_ACK,

	/* uint32_t */
	ETHOSN_MESSAGE_DELAY,

	/* void */
	ETHOSN_MESSAGE_PING,
	ETHOSN_MESSAGE_PONG,

	ETHOSN_MESSAGE_FW_HW_CAPS_REQUEST,
	ETHOSN_MESSAGE_FW_HW_CAPS_RESPONSE,

	ETHOSN_MESSAGE_ERROR_RESPONSE,
	ETHOSN_MESSAGE_MAX
};

/**
 * struct ethosn_message_header - 消息头
 * @var type: 消息类型. @see ethosn_message_type.
 * @var length: Value 数组的长度 (字节).
 *
 * 主机和 Ethos-N 之间的每条消息都应以消息头开始. 消息的类型决定头部后是否跟随额外的有效负载数据.
 */
struct ethosn_message_header {
	uint32_t type;
	uint32_t length;
	uint8_t value[];
};

/******************************************************************************
 * Inference
 ******************************************************************************/
enum ethosn_buffer_type {
	ETHOSN_BUFFER_INPUT,
	ETHOSN_BUFFER_INTERMEDIATE,
	ETHOSN_BUFFER_OUTPUT,
	ETHOSN_BUFFER_CONSTANT,
	ETHOSN_BUFFER_CMD_FW,
	ETHOSN_BUFFER_MAX
};

/**
 * struct ethosn_buffer_desc - 缓冲区描述符
 * @var address: 指向缓冲区的指针.
 * @var size: 缓冲区的大小 (字节).
 * @var type: 缓冲区的类型, 为 ethosn_buffer_type 的成员.
 *            存储为 uint32_t 以具有明确定义的大小,
 *            因为此结构需要在内核模块和固件之间保持一致.
 */
struct ethosn_buffer_desc {
	ethosn_address_t address;
	uint32_t size;
	uint32_t type;
};

/**
 * struct ethosn_buffer_array - 动态大小缓冲区数组
 * @var num_buffers: 缓冲区数量.
 * @var buffers: 缓冲区描述符数组.
 */
struct ethosn_buffer_array {
	uint32_t num_buffers;
	struct ethosn_buffer_desc buffers[];
};

/**
 * 推理状态.
 */
enum ethosn_inference_status { ETHOSN_INFERENCE_STATUS_OK, ETHOSN_INFERENCE_STATUS_ERROR, ETHOSN_INFERENCE_STATUS_MAX };

/**
 * struct ethosn_message_inference_request - 推理请求消息
 * @var user_argument: 用户参数.
 * @var buffer_array: 指向缓冲区数组的指针. @see ethosn_buffer_header.
 *
 * 遵循 ethosn_message_header.
 */
struct ethosn_message_inference_request {
	uint64_t user_argument;
	ethosn_address_t buffer_array;
};

/**
 * struct ethosn_message_inference_response - 推理响应消息
 * @var user_argument: 用户参数.
 * @var status: 推理状态.
 * @var cycle_count: 推理所用的周期数 (由固件测量).
 *
 * 遵循 ethosn_message_header.
 */
struct ethosn_message_inference_response {
	uint64_t user_argument;
	uint32_t status;
	uint64_t cycle_count;
};

/******************************************************************************
 * 文本消息日志
 ******************************************************************************/

/**
 * 日志消息的严重性.
 */
enum ethosn_log_severity { ETHOSN_LOG_PANIC, ETHOSN_LOG_ERROR, ETHOSN_LOG_WARNING, ETHOSN_LOG_INFO, ETHOSN_LOG_DEBUG, ETHOSN_LOG_VERBOSE };

/**
 * struct ethosn_message_text - 文本消息
 * @var severity: 日志消息的严重性.
 *
 * 遵循 ethosn_message_type.
 */
struct ethosn_message_text {
	uint32_t severity;
	char text[];
};

/******************************************************************************
 * 性能分析
 ******************************************************************************/

/**
 * 硬件性能分析计数器的最大数量
 */
#define ETHOSN_PROFILING_MAX_HW_COUNTERS 6U

/**
 * struct ethosn_firmware_profiling_configuration - 发送给固件的消息有效负载, 用于 ETHOSN_MESSAGE_CONFIGURE_PROFILING 消息. 描述固件应设置的性能分析配置.
 *
 * @buffer_address: 固件可访问的地址, 指向类型为 ethosn_profiling_buffer 的结构, 固件应将其性能分析数据写入该结构.
 */
struct ethosn_firmware_profiling_configuration {
	bool enable_profiling;
	ethosn_address_t buffer_address;
	uint32_t buffer_size;
	uint32_t num_hw_counters;
	enum ethosn_profiling_hw_counter_types hw_counters[ETHOSN_PROFILING_MAX_HW_COUNTERS];
};

/**
 * struct ethosn_profiling_buffer - 固件的性能分析缓冲区布局.
 * 这是一个循环缓冲区, 固件写入并且内核从中读取. 当固件到达末尾时, 它只是从头开始覆写. 没有机制防止固件覆写内核尚未读取的数据.
 * 这是有意为之, 因为我们不希望固件在等待内核 (尤其是可能没有人读取性能分析数据的另一端!) 时停滞.
 *
 * @firmware_write_index: 固件应写入的下一个条目数组索引. 由固件更新, 并且对内核只读.
 * @entries: 缓冲区的有效负载.
 */
struct ethosn_profiling_buffer {
	union {
		uint32_t firmware_write_index;

		/* 添加填充以确保 firmware_write_index 和 entries 在不同的缓存行上, 因此刷新是独立的. */
		uint8_t padding[ETHOSN_CACHE_LINE_SIZE];
	};
	struct ethosn_profiling_entry entries[];
};

/**
 * struct ethosn_message_profiling_entries - 性能分析条目消息
 * @num_entries: 条目数量.
 * @entries: 条目列表. 见 ethosn_profiling_entry.
 *
 * 遵循 ethosn_message_type.
 */
struct ethosn_message_profiling_entries {
	uint32_t num_entries;
	struct ethosn_profiling_entry entries[];
};

/******************************************************************************
 * 错误报告
 ******************************************************************************/

/**
 * 请求错误状态
 */
enum ethosn_error_status { ETHOSN_ERROR_STATUS_INVALID_STATE, ETHOSN_ERROR_STATUS_INVALID_MESSAGE, ETHOSN_ERROR_STATUS_FAILED, ETHOSN_ERROR_STATUS_MAX };

/**
 * struct ethosn_message_error_response - 错误响应消息
 * @var type: 发生错误的消息类型
 * @var status: 一般错误状态
 *
 * 遵循 ethosn_message_header.
 */
struct ethosn_message_error_response {
	uint32_t type;
	uint32_t status;
};

/*
 * 定义 DL1_GP 寄存器, 用于内核驱动程序和固件之间的特殊目的通信
 */
#define GP_MAILBOX DL1_GP1
#define GP_BOOT_SUCCESS DL1_GP2
#define GP_DEBUG_MONITOR_CHANNEL DL1_GP4
#define GP_MAILBOX_SIZE DL1_GP5
#define GP_COMMAND_STREAM_SIZE DL1_GP6

/* 固件存储在 GP_BOOT_SUCCESS 中的特殊值, 表示它已成功启动. */
#define ETHOSN_FIRMWARE_BOOT_SUCCESS_MAGIC 0x12488421

/*
 * 用于从固件报告故障的结构, 通过 GP 寄存器传递
 * 并因此限制为 8x4 = 32 字节. 因此数据被紧凑地打包以消除未使用的位. 注意字段的顺序是
 * 使得没有字段跨越基本类型边界 (8 字节), 因为这是编译器依赖的行为.
 */
struct ethosn_firmware_dump {
	/* 必须设置为 ETHOSN_FIRMWARE_DUMP_MAGIC, 以表示此结构已由固件填充. */
	uint64_t magic : 32;

	/* 当前正在处理的异常号的 IPSR 寄存器的 ISR 字段.*
     * 我们只有一个额外的中断, 总共 17 个, 所以 5 位就足够了 */
	uint64_t ISR : 5;

	/* 可配置故障状态寄存器的 MemManage 故障状态寄存器中的非保留位 */
	uint64_t CFSR_MMFSR_MMARVALID : 1;
	uint64_t CFSR_MMFSR_MSTKERR : 1;
	uint64_t CFSR_MMFSR_MUNSTKERR : 1;
	uint64_t CFSR_MMFSR_DACCVIOL : 1;
	uint64_t CFSR_MMFSR_IACCVIOL : 1;

	/* 可配置故障状态寄存器的 BusFault 状态寄存器中的非保留位 */
	uint64_t CFSR_BFSR_BFARVALID : 1;
	uint64_t CFSR_BFSR_STKERR : 1;
	uint64_t CFSR_BFSR_UNSTKERR : 1;
	uint64_t CFSR_BFSR_IMPRECISERR : 1;
	uint64_t CFSR_BFSR_PRECISERR : 1;
	uint64_t CFSR_BFSR_IBUSERR : 1;

	/* 可配置故障状态寄存器的 UsageFault 状态寄存器中的非保留位 */
	uint64_t CFSR_UFSR_DIVBYZERO : 1;
	uint64_t CFSR_UFSR_UNALIGNED : 1;
	uint64_t CFSR_UFSR_NOCP : 1;
	uint64_t CFSR_UFSR_INVPC : 1;
	uint64_t CFSR_UFSR_INVSTATE : 1;
	uint64_t CFSR_UFSR_UNDEFINSTR : 1;

	/* 硬件故障状态寄存器的非保留位 */
	uint64_t HFSR_FORCED : 1;
	uint64_t HFSR_VECTTBL : 1;

	uint64_t unused : 8;

	/* 整个 MemManage 故障地址寄存器 */
	uint64_t MMFAR : 32;
	/* 整个 BusFault 地址寄存器 */
	uint64_t BFAR : 32;

	/* TOP_ERR_CAUSE 寄存器的 21 个非保留位 */
	uint64_t TOP_ERR_CAUSE_ENGINE_RAM_CORRECTABLE_ERR : 1;
	uint64_t TOP_ERR_CAUSE_ENGINE_RAM_UNCORRECTABLE_ERR : 1;
	uint64_t TOP_ERR_CAUSE_TOP_TOLERABLE_RAM_ERR : 1;
	uint64_t TOP_ERR_CAUSE_TOP_RECOVERABLE_RAM_ERR : 1;
	uint64_t TOP_ERR_CAUSE_MCU_LOCKUP_ERR : 1;
	uint64_t TOP_ERR_CAUSE_MCU_INSTR_ERR : 1;
	uint64_t TOP_ERR_CAUSE_MCU_DATA_READ_ERR : 1;
	uint64_t TOP_ERR_CAUSE_MCU_DATA_WRITE_ERR : 1;
	uint64_t TOP_ERR_CAUSE_DMA_READ_ERR : 1;
	uint64_t TOP_ERR_CAUSE_DMA_WRITE_ERR : 1;
	uint64_t TOP_ERR_CAUSE_STASH_TRANSLATION_ERR : 1;
	uint64_t TOP_ERR_CAUSE_DMA_QUEUE_PROGRAMMING_ERR : 1;
	uint64_t TOP_ERR_CAUSE_PWRCTLR_ACTIVE_PROGRAMMING_ERR : 1;
	uint64_t TOP_ERR_CAUSE_STASH_TRANS_PROGRAMMING_ERR : 1;
	uint64_t TOP_ERR_CAUSE_TSU_EVENT_OVERFLOW_ERR : 1;
	uint64_t TOP_ERR_CAUSE_STRIPE_PROGRAMMING_ERR : 1;
	uint64_t TOP_ERR_CAUSE_STRIPE_WRITE_WHILE_BUSY_ERR : 1;
	uint64_t TOP_ERR_CAUSE_BLOCK_PROGRAMMING_ERR : 1;
	uint64_t TOP_ERR_CAUSE_BLOCK_WRITE_WHILE_BUSY_ERR : 1;
	uint64_t TOP_ERR_CAUSE_SHADOW_ERR : 1;
	uint64_t TOP_ERR_CAUSE_ENGINE_FUNC_ERR : 1;

	/* TOP_ERR_ADDRESS 寄存器的 24 个非保留位 */
	uint64_t TOP_ERR_ADDRESS_ADDRESS : 10;
	uint64_t TOP_ERR_ADDRESS_BANK : 3;
	uint64_t TOP_ERR_ADDRESS_NCU_MCU_ICACHE_TAG : 1;
	uint64_t TOP_ERR_ADDRESS_NCU_MCU_ICACHE_DATA : 1;
	uint64_t TOP_ERR_ADDRESS_NCU_MCU_DCACHE_TAG : 1;
	uint64_t TOP_ERR_ADDRESS_NCU_MCU_DCACHE_DATA : 1;
	uint64_t TOP_ERR_ADDRESS_DFC_ROB : 1;
	uint64_t TOP_ERR_ADDRESS_DFC_COMPRESSOR_SIM : 1;
	uint64_t TOP_ERR_ADDRESS_DFC_COMPRESSOR_REM : 1;
	uint64_t TOP_ERR_ADDRESS_DFC_COMPRESSOR_UNARY : 1;
	uint64_t TOP_ERR_ADDRESS_DFC_DECOMPRESSOR : 1;
	uint64_t TOP_ERR_ADDRESS_ERR_MULTI : 1;
	uint64_t TOP_ERR_ADDRESS_ERR_UNCORRECTED : 1;

	/* 一个 CE 的一位 (LSB = CE 0, MSB = CE 7), 表示它是否有任何错误.
     * 更多细节用于第一个有错误的 CE 下面存储.
     */
	uint64_t cesWithError : 8;

	/* 第一个发生错误的 CE 的 CE_ERR_CAUSE 寄存器的 14 个非保留位 (如果有) */
	uint64_t CE_ERR_CAUSE_ENGINE_RAM_CORRECTABLE_ERR : 1;
	uint64_t CE_ERR_CAUSE_ENGINE_RAM_UNCORRECTABLE_ERR : 1;
	uint64_t CE_ERR_CAUSE_MCU_LOCKUP_ERR : 1;
	uint64_t CE_ERR_CAUSE_MCU_INSTR_ERR : 1;
	uint64_t CE_ERR_CAUSE_MCU_DATA_READ_ERR : 1;
	uint64_t CE_ERR_CAUSE_MCU_DATA_WRITE_ERR : 1;
	uint64_t CE_ERR_CAUSE_UDMA_LOAD_ERR : 1;
	uint64_t CE_ERR_CAUSE_UDMA_STORE_ERR : 1;
	uint64_t CE_ERR_CAUSE_MCU_ILLEGAL_COPROC_ERR : 1;
	uint64_t CE_ERR_CAUSE_UDMA_COLLISION_ERR : 1;
	uint64_t CE_ERR_CAUSE_RF_RD_COLLISION_ERR : 1;
	uint64_t CE_ERR_CAUSE_RF_WR_COLLISION_ERR : 1;
	uint64_t CE_ERR_CAUSE_VE_DIV_0_ERR : 1;
	uint64_t CE_ERR_CAUSE_PLE_LANE_ERR : 1;

	/* 第一个发生错误的 CE 的 CE_ERR_ADDRESS 寄存器的 31 个非保留位 (如果有) */
	uint64_t CE_ERR_ADDRESS_ADDRESS : 12;
	uint64_t CE_ERR_ADDRESS_BANK : 3;
	uint64_t CE_ERR_ADDRESS_DFC_EMC0 : 1;
	uint64_t CE_ERR_ADDRESS_DFC_EMC1 : 1;
	uint64_t CE_ERR_ADDRESS_DFC_EMC2 : 1;
	uint64_t CE_ERR_ADDRESS_DFC_EMC3 : 1;
	uint64_t CE_ERR_ADDRESS_MCE_OFM0 : 1;
	uint64_t CE_ERR_ADDRESS_MCE_OFM1 : 1;
	uint64_t CE_ERR_ADDRESS_MCE_OFM2 : 1;
	uint64_t CE_ERR_ADDRESS_MCE_OFM3 : 1;
	uint64_t CE_ERR_ADDRESS_PLE_INPUT0 : 1;
	uint64_t CE_ERR_ADDRESS_PLE_INPUT1 : 1;
	uint64_t CE_ERR_ADDRESS_PLE_INPUT2 : 1;
	uint64_t CE_ERR_ADDRESS_PLE_INPUT3 : 1;
	uint64_t CE_ERR_ADDRESS_PLE_OUTPUT : 1;
	uint64_t CE_ERR_ADDRESS_PLE_MCU : 1;
	uint64_t CE_ERR_ADDRESS_ERR_MULTI : 1;
	uint64_t CE_ERR_ADDRESS_ERR_UNCORRECTED : 1;

	/* 发生故障时的堆叠程序计数器值, 即故障触发之前的值. */
	uint64_t pc : 30;
};

#define ETHOSN_FIRMWARE_DUMP_MAGIC 0x12345678

#if defined(__cplusplus)
static_assert(sizeof(ethosn_firmware_dump) <= 32, "ethosn_firmware_dump 结构过大, 无法适用于 GP 寄存器");
#endif

#pragma pack(pop)

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif /* _ETHOSN_FIRMWARE_H_ */
