/*
 * ai_ml_engine.h - HER OS AI/ML Engine Header
 *
 * Defines the core structures, functions, and constants for the AI/ML
 * engine that provides intelligent prediction, learning, and adaptation
 * capabilities. Follows Linux kernel coding style (K&R, tabs, block comments).
 *
 * Author: HER OS Project
 */

#ifndef AI_ML_ENGINE_H
#define AI_ML_ENGINE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sqlite3.h>
#include <json-c/json.h>
#include <glib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <uuid/uuid.h>
#include <math.h>

/* Configuration constants */
#define AI_ML_CONFIG_FILE "/etc/heros/ai_ml_config.json"
#define AI_ML_LOG_FILE "/var/log/ai_ml_engine.log"
#define AI_ML_SOCKET_PATH "/tmp/heros_ai_ml.sock"

/* Model types */
typedef enum {
    MODEL_TYPE_REGRESSION = 0,
    MODEL_TYPE_CLASSIFICATION,
    MODEL_TYPE_CLUSTERING,
    MODEL_TYPE_SEQUENCE,
    MODEL_TYPE_TRANSFORMER,
    MODEL_TYPE_CNN,
    MODEL_TYPE_RNN,
    MODEL_TYPE_MAX
} model_type_t;

/* Architecture types */
typedef enum {
    ARCHITECTURE_LINEAR = 0,
    ARCHITECTURE_RANDOM_FOREST,
    ARCHITECTURE_SVM,
    ARCHITECTURE_NEURAL_NETWORK,
    ARCHITECTURE_TRANSFORMER,
    ARCHITECTURE_CNN,
    ARCHITECTURE_RNN,
    ARCHITECTURE_LSTM,
    ARCHITECTURE_GRU,
    ARCHITECTURE_MAX
} architecture_type_t;

/* RL algorithm types */
typedef enum {
    RL_ALGORITHM_Q_LEARNING = 0,
    RL_ALGORITHM_DQN,
    RL_ALGORITHM_PPO,
    RL_ALGORITHM_A2C,
    RL_ALGORITHM_SAC,
    RL_ALGORITHM_MAX
} rl_algorithm_t;

/* Engine state */
typedef enum {
    AI_ML_STATE_UNINITIALIZED = 0,
    AI_ML_STATE_INITIALIZING,
    AI_ML_STATE_READY,
    AI_ML_STATE_TRAINING,
    AI_ML_STATE_INFERENCE,
    AI_ML_STATE_ERROR,
    AI_ML_STATE_SHUTDOWN
} ai_ml_state_t;

/*
 * Statistical model structure
 */
typedef struct {
    char *model_id;              /* Unique model identifier */
    char *model_type;            /* Model type (regression, classification, etc.) */
    json_object *parameters;     /* Model parameters */
    json_object *features;       /* Feature definitions */
    double accuracy;             /* Model accuracy score */
    time_t last_trained;         /* Last training timestamp */
    int training_samples;        /* Number of training samples */
    double *weights;             /* Model weights */
    int num_weights;             /* Number of weights */
    double bias;                 /* Model bias term */
} statistical_model_t;

/*
 * Prediction result structure
 */
typedef struct {
    char *prediction_id;         /* Unique prediction identifier */
    char *model_id;              /* Source model identifier */
    json_object *input_features; /* Input features */
    json_object *prediction;     /* Prediction output */
    double confidence;           /* Prediction confidence */
    time_t timestamp;            /* Prediction timestamp */
    double prediction_value;     /* Numeric prediction value */
    char *prediction_label;      /* Classification label */
} prediction_result_t;

/*
 * Neural network model structure
 */
typedef struct {
    char *model_id;              /* Unique model identifier */
    char *architecture;          /* Network architecture */
    json_object *layers;         /* Layer definitions */
    json_object *weights;        /* Model weights */
    json_object *hyperparameters; /* Training hyperparameters */
    double loss;                 /* Current loss value */
    int epochs_trained;          /* Number of training epochs */
    time_t last_trained;         /* Last training timestamp */
    int input_dim;               /* Input dimension */
    int output_dim;              /* Output dimension */
    int num_layers;              /* Number of layers */
    double *layer_weights;       /* Layer weights */
    double *layer_biases;        /* Layer biases */
    int *layer_sizes;            /* Layer sizes */
} neural_network_t;

/*
 * Neural network inference structure
 */
typedef struct {
    char *inference_id;          /* Unique inference identifier */
    char *model_id;              /* Source model identifier */
    json_object *input_data;     /* Input data */
    json_object *output_data;    /* Output predictions */
    double inference_time;       /* Inference time in milliseconds */
    time_t timestamp;            /* Inference timestamp */
    double *input_vector;        /* Input vector */
    double *output_vector;       /* Output vector */
    int input_size;              /* Input size */
    int output_size;             /* Output size */
} neural_inference_t;

/*
 * NLP model structure
 */
typedef struct {
    char *model_id;              /* Unique model identifier */
    char *language;              /* Target language */
    char *task_type;             /* NLP task type */
    json_object *vocabulary;     /* Vocabulary and embeddings */
    json_object *model_data;     /* Model-specific data */
    double performance_score;    /* Model performance score */
    time_t last_updated;         /* Last update timestamp */
    char **vocab_words;          /* Vocabulary words */
    double **embeddings;         /* Word embeddings */
    int vocab_size;              /* Vocabulary size */
    int embedding_dim;           /* Embedding dimension */
} nlp_model_t;

/*
 * NLP processing result structure
 */
typedef struct {
    char *processing_id;         /* Unique processing identifier */
    char *model_id;              /* Source model identifier */
    char *input_text;            /* Input text */
    json_object *intent;         /* Extracted intent */
    json_object *entities;       /* Named entities */
    json_object *sentiment;      /* Sentiment analysis */
    double confidence;           /* Processing confidence */
    time_t timestamp;            /* Processing timestamp */
    char *extracted_intent;      /* Intent string */
    double sentiment_score;      /* Sentiment score */
    char **named_entities;       /* Named entities array */
    int num_entities;            /* Number of entities */
} nlp_result_t;

/*
 * RL agent structure
 */
typedef struct {
    char *agent_id;              /* Unique agent identifier */
    char *policy_type;           /* Policy type (Q-learning, policy gradient, etc.) */
    json_object *state_space;    /* State space definition */
    json_object *action_space;   /* Action space definition */
    json_object *policy;         /* Current policy */
    json_object *value_function; /* Value function */
    double total_reward;         /* Cumulative reward */
    int episodes_completed;      /* Number of completed episodes */
    time_t last_update;          /* Last policy update timestamp */
    int state_dim;               /* State dimension */
    int action_dim;              /* Action dimension */
    double **q_table;            /* Q-table for Q-learning */
    double *policy_weights;      /* Policy weights */
    double learning_rate;        /* Learning rate */
    double discount_factor;      /* Discount factor */
} rl_agent_t;

/*
 * RL experience structure
 */
typedef struct {
    char *experience_id;         /* Unique experience identifier */
    char *agent_id;              /* Source agent identifier */
    json_object *state;          /* State observation */
    json_object *action;         /* Action taken */
    double reward;               /* Reward received */
    json_object *next_state;     /* Next state observation */
    gboolean terminal;           /* Whether episode ended */
    time_t timestamp;            /* Experience timestamp */
    double *state_vector;        /* State vector */
    double *action_vector;       /* Action vector */
    double *next_state_vector;   /* Next state vector */
    int state_size;              /* State size */
    int action_size;             /* Action size */
} rl_experience_t;

/*
 * Training data structure
 */
typedef struct {
    char *data_id;               /* Unique data identifier */
    json_object *features;       /* Feature data */
    json_object *labels;         /* Label data */
    int num_samples;             /* Number of samples */
    int num_features;            /* Number of features */
    time_t timestamp;            /* Data timestamp */
    double **feature_matrix;     /* Feature matrix */
    double *label_vector;        /* Label vector */
} training_data_t;

/*
 * Model performance metrics
 */
typedef struct {
    char *model_id;              /* Model identifier */
    double accuracy;             /* Model accuracy */
    double precision;            /* Model precision */
    double recall;               /* Model recall */
    double f1_score;             /* F1 score */
    double mse;                  /* Mean squared error */
    double mae;                  /* Mean absolute error */
    int num_predictions;         /* Number of predictions */
    time_t last_evaluation;      /* Last evaluation timestamp */
} model_metrics_t;

/*
 * AI/ML engine configuration
 */
typedef struct {
    gboolean enable_statistical_models;
    gboolean enable_neural_networks;
    gboolean enable_nlp;
    gboolean enable_reinforcement_learning;
    gboolean enable_federated_learning;
    gboolean enable_model_management;
    int prediction_horizon;
    int training_interval;
    double model_update_threshold;
    double privacy_budget;
    char *log_file;
    char *config_file;
} ai_ml_config_t;

/*
 * Performance metrics structure
 */
typedef struct {
    int predictions_made;
    int models_trained;
    int inferences_performed;
    int nlp_requests_processed;
    int rl_episodes_completed;
    time_t start_time;
    double avg_prediction_time;
    double avg_training_time;
    double avg_inference_time;
    double avg_nlp_time;
    double avg_rl_episode_time;
} performance_metrics_t;

/*
 * Global AI/ML engine state
 */
typedef struct {
    ai_ml_state_t state;
    ai_ml_config_t config;
    
    /* Model storage */
    GHashTable *statistical_models;  /* model_id -> statistical_model */
    GHashTable *neural_networks;     /* model_id -> neural_network */
    GHashTable *nlp_models;          /* model_id -> nlp_model */
    GHashTable *rl_agents;           /* agent_id -> rl_agent */
    
    /* Training data */
    GHashTable *training_datasets;   /* data_id -> training_data */
    
    /* Performance tracking */
    GHashTable *model_metrics;       /* model_id -> model_metrics */
    performance_metrics_t metrics;
    
    /* Threading */
    pthread_mutex_t engine_mutex;
    pthread_mutex_t models_mutex;
    pthread_mutex_t training_mutex;
    pthread_mutex_t metrics_mutex;
    pthread_t training_thread;
    pthread_t inference_thread;
    pthread_t rl_thread;
    gboolean threads_running;
    
    /* Database connection */
    sqlite3 *ai_ml_db;
} ai_ml_engine_t;

/* Function declarations */

/*
 * Core engine functions
 */
int ai_ml_engine_init(const char *config_file);
void ai_ml_engine_cleanup(void);
ai_ml_engine_t *get_ai_ml_engine(void);
int ai_ml_engine_start(void);
int ai_ml_engine_stop(void);

/*
 * Statistical model functions
 */
int create_statistical_model(const char *model_type, const char *model_id, statistical_model_t **model);
int train_statistical_model(const char *model_id, training_data_t *data);
int predict_with_statistical_model(const char *model_id, json_object *features, prediction_result_t **result);
int evaluate_statistical_model(const char *model_id, training_data_t *test_data, model_metrics_t **metrics);
int save_statistical_model(const char *model_id);
int load_statistical_model(const char *model_id, statistical_model_t **model);

/*
 * Neural network functions
 */
int create_neural_network(const char *architecture, const char *model_id, neural_network_t **model);
int train_neural_network(const char *model_id, training_data_t *data, int epochs);
int infer_with_neural_network(const char *model_id, json_object *input, neural_inference_t **result);
int evaluate_neural_network(const char *model_id, training_data_t *test_data, model_metrics_t **metrics);
int save_neural_network(const char *model_id);
int load_neural_network(const char *model_id, neural_network_t **model);

/*
 * NLP functions
 */
int create_nlp_model(const char *task_type, const char *language, const char *model_id, nlp_model_t **model);
int train_nlp_model(const char *model_id, training_data_t *data);
int process_nlp_request(const char *model_id, const char *text, nlp_result_t **result);
int evaluate_nlp_model(const char *model_id, training_data_t *test_data, model_metrics_t **metrics);
int save_nlp_model(const char *model_id);
int load_nlp_model(const char *model_id, nlp_model_t **model);

/*
 * Reinforcement learning functions
 */
int create_rl_agent(const char *algorithm, const char *agent_id, rl_agent_t **agent);
int train_rl_agent(const char *agent_id, rl_experience_t *experience);
int get_rl_action(const char *agent_id, json_object *state, json_object **action);
int evaluate_rl_agent(const char *agent_id, int num_episodes, double *avg_reward);
int save_rl_agent(const char *agent_id);
int load_rl_agent(const char *agent_id, rl_agent_t **agent);

/*
 * Training data functions
 */
int create_training_dataset(const char *data_id, training_data_t **dataset);
int add_training_samples(const char *data_id, json_object *features, json_object *labels);
int load_training_data(const char *data_id, training_data_t **dataset);
int save_training_data(const char *data_id);
int preprocess_training_data(training_data_t *dataset);

/*
 * Model management functions
 */
int list_models(const char *model_type, GList **models);
int delete_model(const char *model_id);
int compare_models(const char *model1_id, const char *model2_id, model_metrics_t **comparison);
int export_model(const char *model_id, const char *export_path);
int import_model(const char *import_path, char **model_id);

/*
 * Performance monitoring functions
 */
int update_performance_metrics(const char *metric_type, double value);
int get_performance_metrics(performance_metrics_t *metrics);
int get_model_metrics(const char *model_id, model_metrics_t **metrics);
int reset_performance_metrics(void);
int export_performance_report(const char *format, char **report);

/*
 * Configuration functions
 */
int load_ai_ml_config(ai_ml_config_t *config, const char *config_file);
int save_ai_ml_config(ai_ml_config_t *config, const char *config_file);
void free_ai_ml_config(ai_ml_config_t *config);

/*
 * Utility functions
 */
char *generate_model_id(void);
char *generate_prediction_id(void);
char *generate_inference_id(void);
char *generate_agent_id(void);
char *model_type_to_string(model_type_t type);
model_type_t string_to_model_type(const char *type_str);
char *architecture_to_string(architecture_type_t arch);
architecture_type_t string_to_architecture(const char *arch_str);
char *rl_algorithm_to_string(rl_algorithm_t algorithm);
rl_algorithm_t string_to_rl_algorithm(const char *algorithm_str);
void ai_ml_log_message(const char *level, const char *format, ...);
int validate_model(statistical_model_t *model);
int validate_neural_network(neural_network_t *model);
int validate_nlp_model(nlp_model_t *model);
int validate_rl_agent(rl_agent_t *agent);

/*
 * Threading functions
 */
void *training_thread(void *arg);
void *inference_thread(void *arg);
void *rl_thread(void *arg);
int start_ai_ml_threads(void);
int stop_ai_ml_threads(void);

/*
 * Database functions
 */
int init_ai_ml_database(const char *db_path);
void cleanup_ai_ml_database(void);
int store_model_in_database(const char *model_type, const char *model_id, json_object *model_data);
int load_model_from_database(const char *model_type, const char *model_id, json_object **model_data);
int store_training_data_in_database(const char *data_id, training_data_t *data);
int load_training_data_from_database(const char *data_id, training_data_t **data);
int query_model_performance(const char *query, GList **results);

/*
 * Mathematical functions
 */
double sigmoid(double x);
double relu(double x);
double tanh_activation(double x);
double softmax(double *values, int size, int index);
double mean_squared_error(double *predictions, double *targets, int size);
double cross_entropy_loss(double *predictions, double *targets, int size);
void matrix_multiply(double *A, double *B, double *C, int m, int n, int p);
void vector_add(double *A, double *B, double *C, int size);
void vector_scale(double *A, double scalar, double *B, int size);

/*
 * Error handling functions
 */
void set_ai_ml_error(int error_code, const char *error_message);
int get_ai_ml_error(void);
const char *get_ai_ml_error_message(void);

/* Global engine instance */
extern ai_ml_engine_t *g_ai_ml_engine;

/* Error handling */
extern int g_ai_ml_error_code;
extern char g_ai_ml_error_message[256];

#endif /* AI_ML_ENGINE_H */
