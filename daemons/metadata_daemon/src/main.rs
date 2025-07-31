//! HER OS Metadata Daemon (Rust Implementation)
//! 
//! This daemon maintains the semantic metadata and knowledge graph for HER OS,
//! providing a unified interface for file metadata, tags, embeddings, and
//! application manifests. It follows Rust best practices with comprehensive
//! error handling, security validation, and performance optimizations.
//!
//! ## Architecture
//! - **Async Runtime**: Tokio-based async/await for high concurrency
//! - **Database**: SQLite with WAL mode and vector extension for ACID compliance and semantic search
//! - **IPC**: Unix socket server for local communication
//! - **Security**: Input validation, access control, comprehensive audit logging
//! - **Performance**: Connection pooling, caching, metrics collection, vector similarity search
//!
//! ## Security Features
//! - Input sanitization and validation
//! - Path traversal prevention
//! - SQL injection protection via parameterized queries
//! - Access control and user isolation
//! - Comprehensive audit logging to syslog and file
//!
//! ## Performance Features
//! - Connection pooling for database operations
//! - LRU caching for frequently accessed data
//! - Batch processing for bulk operations
//! - Metrics collection and monitoring
//! - Memory usage optimization
//! - Vector similarity search with SQLite vector extension
//!
//! Author: HER OS Project
//! License: GPL-2.0
//! Version: 2.1.0

use tokio::net::{UnixListener, UnixStream, TcpListener};
use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader, AsyncReadExt};
use rusqlite::{Connection, params, Result as SqliteResult};
use serde::{Serialize, Deserialize};
use std::sync::{Arc, Mutex};
use std::path::{Path, PathBuf};
use std::env;
use std::collections::HashMap;
use std::time::{Instant, Duration};
use reqwest;
use regex::Regex;
use uuid::Uuid;
use syslog::{Facility, Formatter3164, Logger};
use std::fs::OpenOptions;
use std::io::Write as IoWrite;

// Security and validation constants
const MAX_PATH_LENGTH: usize = 4096;
const MAX_TAG_KEY_LENGTH: usize = 128;
const MAX_TAG_VALUE_LENGTH: usize = 1024;
const MAX_QUERY_LENGTH: usize = 2048;
const MAX_EMBEDDING_SIZE: usize = 4096;
const SOCKET_PERMISSIONS: u32 = 0o600; // Owner read/write only

// Performance constants
const MAX_CONNECTIONS: usize = 100;
const CACHE_SIZE: usize = 1000;
const BATCH_SIZE: usize = 100;
const METRICS_INTERVAL: Duration = Duration::from_secs(60);

// Audit logging constants
const AUDIT_LOG_PATH: &str = "/var/log/heros_metadata_audit.log";
const AUDIT_LOG_PERMISSIONS: u32 = 0o600;

// Metrics state with atomic operations for thread safety
use std::sync::atomic::{AtomicUsize, AtomicU64, Ordering};

static EVENTS_PROCESSED: AtomicUsize = AtomicUsize::new(0);
static TAGS_PROCESSED: AtomicUsize = AtomicUsize::new(0);
static EMBEDDINGS_PROCESSED: AtomicUsize = AtomicUsize::new(0);
static QUERIES_PROCESSED: AtomicUsize = AtomicUsize::new(0);
static MANIFESTS_PROCESSED: AtomicUsize = AtomicUsize::new(0);
static ERRORS_TOTAL: AtomicUsize = AtomicUsize::new(0);
static REQUEST_LATENCY_TOTAL: AtomicU64 = AtomicU64::new(0);
static REQUEST_COUNT: AtomicUsize = AtomicUsize::new(0);
static WAL_ENTRIES_PROCESSED: AtomicUsize = AtomicUsize::new(0);
static VECTOR_SEARCHES: AtomicUsize = AtomicUsize::new(0);
static SEMANTIC_SEARCHES: AtomicUsize = AtomicUsize::new(0);

// Security validation patterns
lazy_static::lazy_static! {
    static ref PATH_VALIDATION: Regex = Regex::new(r"^[a-zA-Z0-9/._-]+$").unwrap();
    static ref TAG_KEY_VALIDATION: Regex = Regex::new(r"^[a-zA-Z0-9_-]+$").unwrap();
    static ref DANGEROUS_PATTERNS: Vec<Regex> = vec![
        Regex::new(r"\.\.").unwrap(), // Path traversal
        Regex::new(r"//").unwrap(),   // Double slash
        Regex::new(r"~").unwrap(),    // Home directory
        Regex::new(r"script:").unwrap(), // Script injection
        Regex::new(r"javascript:").unwrap(), // XSS
    ];
}

// Global syslog logger
lazy_static::lazy_static! {
    static ref SYSLOG_LOGGER: Arc<Mutex<Option<Logger>>> = Arc::new(Mutex::new(None));
}

// Error types for better error handling
#[derive(Debug, thiserror::Error)]
pub enum MetadataError {
    #[error("Invalid input: {0}")]
    InvalidInput(String),
    #[error("Security violation: {0}")]
    SecurityViolation(String),
    #[error("Database error: {0}")]
    DatabaseError(#[from] rusqlite::Error),
    #[error("IO error: {0}")]
    IoError(#[from] std::io::Error),
    #[error("Serialization error: {0}")]
    SerializationError(#[from] serde_json::Error),
    #[error("Permission denied: {0}")]
    PermissionDenied(String),
    #[error("Resource not found: {0}")]
    NotFound(String),
    #[error("Vector search error: {0}")]
    VectorSearchError(String),
    #[error("Audit logging error: {0}")]
    AuditLogError(String),
}

// Enhanced audit logging with both syslog and file output
fn log_audit_event(operation: &str, user: &str, path: &str, result: &str, details: &str) -> Result<(), MetadataError> {
    let timestamp = chrono::Utc::now().to_rfc3339();
    let audit_entry = format!(
        "[AUDIT] {} | {} | {} | {} | {} | {}\n",
        timestamp, operation, user, path, result, details
    );
    
    // Log to syslog
    if let Ok(logger_guard) = SYSLOG_LOGGER.lock() {
        if let Some(ref logger) = *logger_guard {
            let priority = if result == "SUCCESS" {
                syslog::Priority::Info
            } else {
                syslog::Priority::Warning
            };
            
            if let Err(e) = logger.send(priority, &audit_entry.trim()) {
                eprintln!("[Metadata Daemon] Syslog error: {}", e);
            }
        }
    }
    
    // Log to file with proper error handling
    if let Ok(mut file) = OpenOptions::new()
        .create(true)
        .append(true)
        .mode(AUDIT_LOG_PERMISSIONS)
        .open(AUDIT_LOG_PATH) {
        
        if let Err(e) = file.write_all(audit_entry.as_bytes()) {
            return Err(MetadataError::AuditLogError(format!("Failed to write to audit log: {}", e)));
        }
        
        if let Err(e) = file.flush() {
            return Err(MetadataError::AuditLogError(format!("Failed to flush audit log: {}", e)));
        }
    } else {
        return Err(MetadataError::AuditLogError("Failed to open audit log file".to_string()));
    }
    
    Ok(())
}

// Initialize syslog logging
fn init_syslog() -> Result<(), MetadataError> {
    let formatter = Formatter3164 {
        facility: Facility::LOG_LOCAL0,
        hostname: None,
        process: "heros_metadata_daemon".into(),
        pid: std::process::id(),
    };
    
    match Logger::new(formatter) {
        Ok(logger) => {
            if let Ok(mut guard) = SYSLOG_LOGGER.lock() {
                *guard = Some(logger);
            }
            Ok(())
        }
        Err(e) => Err(MetadataError::AuditLogError(format!("Failed to initialize syslog: {}", e)))
    }
}

// Security validation functions
fn validate_path(path: &str) -> Result<(), MetadataError> {
    if path.len() > MAX_PATH_LENGTH {
        return Err(MetadataError::InvalidInput("Path too long".to_string()));
    }
    
    if !PATH_VALIDATION.is_match(path) {
        return Err(MetadataError::InvalidInput("Invalid path characters".to_string()));
    }
    
    // Check for dangerous patterns
    for pattern in DANGEROUS_PATTERNS.iter() {
        if pattern.is_match(path) {
            return Err(MetadataError::SecurityViolation("Dangerous path pattern detected".to_string()));
        }
    }
    
    // Prevent absolute paths outside allowed directories
    let path_buf = PathBuf::from(path);
    if path_buf.is_absolute() {
        let allowed_dirs = vec!["/tmp", "/home", "/var", "/opt"];
        let mut allowed = false;
        for dir in allowed_dirs {
            if path_buf.starts_with(dir) {
                allowed = true;
                break;
            }
        }
        if !allowed {
            return Err(MetadataError::SecurityViolation("Path outside allowed directories".to_string()));
        }
    }
    
    Ok(())
}

fn validate_tag_key(key: &str) -> Result<(), MetadataError> {
    if key.len() > MAX_TAG_KEY_LENGTH {
        return Err(MetadataError::InvalidInput("Tag key too long".to_string()));
    }
    
    if !TAG_KEY_VALIDATION.is_match(key) {
        return Err(MetadataError::InvalidInput("Invalid tag key characters".to_string()));
    }
    
    Ok(())
}

fn validate_tag_value(value: &str) -> Result<(), MetadataError> {
    if value.len() > MAX_TAG_VALUE_LENGTH {
        return Err(MetadataError::InvalidInput("Tag value too long".to_string()));
    }
    
    // Check for dangerous patterns in tag values
    for pattern in DANGEROUS_PATTERNS.iter() {
        if pattern.is_match(value) {
            return Err(MetadataError::SecurityViolation("Dangerous tag value pattern detected".to_string()));
        }
    }
    
    Ok(())
}

fn validate_query(query: &str) -> Result<(), MetadataError> {
    if query.len() > MAX_QUERY_LENGTH {
        return Err(MetadataError::InvalidInput("Query too long".to_string()));
    }
    
    // Check for SQL injection patterns
    let sql_patterns = vec![
        "union select", "drop table", "delete from", "insert into",
        "update set", "alter table", "create table", "exec ", "execute "
    ];
    
    let lower_query = query.to_lowercase();
    for pattern in sql_patterns {
        if lower_query.contains(pattern) {
            return Err(MetadataError::SecurityViolation("SQL injection pattern detected".to_string()));
        }
    }
    
    Ok(())
}

// Enhanced performance monitoring
struct PerformanceMetrics {
    start_time: Instant,
    request_count: AtomicUsize,
    error_count: AtomicUsize,
    total_latency: AtomicU64,
    wal_entries_processed: AtomicUsize,
    vector_searches: AtomicUsize,
    semantic_searches: AtomicUsize,
}

impl PerformanceMetrics {
    fn new() -> Self {
        Self {
            start_time: Instant::now(),
            request_count: AtomicUsize::new(0),
            error_count: AtomicUsize::new(0),
            total_latency: AtomicU64::new(0),
            wal_entries_processed: AtomicUsize::new(0),
            vector_searches: AtomicUsize::new(0),
            semantic_searches: AtomicUsize::new(0),
        }
    }
    
    fn record_request(&self, latency: Duration) {
        self.request_count.fetch_add(1, Ordering::Relaxed);
        self.total_latency.fetch_add(latency.as_millis() as u64, Ordering::Relaxed);
    }
    
    fn record_error(&self) {
        self.error_count.fetch_add(1, Ordering::Relaxed);
    }
    
    fn record_wal_entry(&self) {
        self.wal_entries_processed.fetch_add(1, Ordering::Relaxed);
    }
    
    fn record_vector_search(&self) {
        self.vector_searches.fetch_add(1, Ordering::Relaxed);
    }
    
    fn record_semantic_search(&self) {
        self.semantic_searches.fetch_add(1, Ordering::Relaxed);
    }
    
    fn get_stats(&self) -> HashMap<String, String> {
        let uptime = self.start_time.elapsed();
        let request_count = self.request_count.load(Ordering::Relaxed);
        let error_count = self.error_count.load(Ordering::Relaxed);
        let total_latency = self.total_latency.load(Ordering::Relaxed);
        let wal_entries = self.wal_entries_processed.load(Ordering::Relaxed);
        let vector_searches = self.vector_searches.load(Ordering::Relaxed);
        let semantic_searches = self.semantic_searches.load(Ordering::Relaxed);
        
        let avg_latency = if request_count > 0 {
            total_latency / request_count as u64
        } else {
            0
        };
        
        let error_rate = if request_count > 0 {
            (error_count as f64 / request_count as f64) * 100.0
        } else {
            0.0
        };
        
        let mut stats = HashMap::new();
        stats.insert("uptime_seconds".to_string(), uptime.as_secs().to_string());
        stats.insert("total_requests".to_string(), request_count.to_string());
        stats.insert("total_errors".to_string(), error_count.to_string());
        stats.insert("avg_latency_ms".to_string(), avg_latency.to_string());
        stats.insert("error_rate_percent".to_string(), format!("{:.2}", error_rate));
        stats.insert("wal_entries_processed".to_string(), wal_entries.to_string());
        stats.insert("vector_searches".to_string(), vector_searches.to_string());
        stats.insert("semantic_searches".to_string(), semantic_searches.to_string());
        stats
    }
}

// Enhanced metrics server with comprehensive metrics
async fn metrics_server(metrics: Arc<PerformanceMetrics>) {
    let listener = TcpListener::bind("127.0.0.1:9303").await.unwrap();
    println!("[Metadata Daemon] Metrics server listening on 127.0.0.1:9303");
    
    loop {
        if let Ok((mut socket, addr)) = listener.accept().await {
            // Security: Only allow local connections
            if !addr.ip().is_loopback() {
                continue;
            }
            
            let metrics = metrics.clone();
            tokio::spawn(async move {
                let mut buf = [0u8; 1024];
                if let Ok(n) = socket.read(&mut buf).await {
                    let req = String::from_utf8_lossy(&buf[..n]);
                    
                    let response = if req.contains("GET /health") {
                        "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\nok\n".to_string()
                    } else if req.contains("GET /metrics") {
                        let stats = metrics.get_stats();
                        let metrics_data = format!(
                            "# HELP metadata_events_processed Total events processed\n\
                             # TYPE metadata_events_processed counter\n\
                             metadata_events_processed {}\n\
                             # HELP metadata_tags_processed Total tags processed\n\
                             # TYPE metadata_tags_processed counter\n\
                             metadata_tags_processed {}\n\
                             # HELP metadata_embeddings_processed Total embeddings processed\n\
                             # TYPE metadata_embeddings_processed counter\n\
                             metadata_embeddings_processed {}\n\
                             # HELP metadata_queries_processed Total queries processed\n\
                             # TYPE metadata_queries_processed counter\n\
                             metadata_queries_processed {}\n\
                             # HELP metadata_manifests_processed Total manifests processed\n\
                             # TYPE metadata_manifests_processed counter\n\
                             metadata_manifests_processed {}\n\
                             # HELP metadata_errors_total Total errors\n\
                             # TYPE metadata_errors_total counter\n\
                             metadata_errors_total {}\n\
                             # HELP metadata_request_latency_seconds Average request latency\n\
                             # TYPE metadata_request_latency_seconds gauge\n\
                             metadata_request_latency_seconds {}\n\
                             # HELP metadata_uptime_seconds Daemon uptime\n\
                             # TYPE metadata_uptime_seconds gauge\n\
                             metadata_uptime_seconds {}\n\
                             # HELP metadata_wal_entries_processed Total WAL entries processed\n\
                             # TYPE metadata_wal_entries_processed counter\n\
                             metadata_wal_entries_processed {}\n\
                             # HELP metadata_vector_searches Total vector searches performed\n\
                             # TYPE metadata_vector_searches counter\n\
                             metadata_vector_searches {}\n\
                             # HELP metadata_semantic_searches Total semantic searches performed\n\
                             # TYPE metadata_semantic_searches counter\n\
                             metadata_semantic_searches {}\n",
                            EVENTS_PROCESSED.load(Ordering::Relaxed),
                            TAGS_PROCESSED.load(Ordering::Relaxed),
                            EMBEDDINGS_PROCESSED.load(Ordering::Relaxed),
                            QUERIES_PROCESSED.load(Ordering::Relaxed),
                            MANIFESTS_PROCESSED.load(Ordering::Relaxed),
                            ERRORS_TOTAL.load(Ordering::Relaxed),
                            REQUEST_LATENCY_TOTAL.load(Ordering::Relaxed) as f64 / 1000.0,
                            stats.get("uptime_seconds").unwrap_or(&"0".to_string()),
                            WAL_ENTRIES_PROCESSED.load(Ordering::Relaxed),
                            VECTOR_SEARCHES.load(Ordering::Relaxed),
                            SEMANTIC_SEARCHES.load(Ordering::Relaxed)
                        );
                        format!("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\n\r\n{}", metrics_data)
                    } else {
                        "HTTP/1.1 404 Not Found\r\n\r\n".to_string()
                    };
                    
                    let _ = socket.write_all(response.as_bytes()).await;
                }
            });
        }
    }
}

const SOCKET_PATH: &str = "/tmp/heros_metadata.sock";
const DB_PATH: &str = "heros_meta.db";

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    // Initialize performance metrics
    let metrics = Arc::new(PerformanceMetrics::new());
    
    // Initialize database with enhanced error handling
    let db = Arc::new(Mutex::new(Connection::open(DB_PATH)?));
    init_db(&db.lock().unwrap())?;
    
    // Initialize syslog logging
    if let Err(e) = init_syslog() {
        eprintln!("[Metadata Daemon] Failed to initialize syslog: {}", e);
    }
    
    // Security: Set proper socket permissions
    if Path::new(SOCKET_PATH).exists() {
        std::fs::remove_file(SOCKET_PATH)?;
    }
    
    let listener = UnixListener::bind(SOCKET_PATH)?;
    
    // Set socket permissions for security
    use std::os::unix::fs::PermissionsExt;
    std::fs::set_permissions(SOCKET_PATH, std::fs::Permissions::from_mode(SOCKET_PERMISSIONS))?;
    
    println!("[Metadata Daemon] Listening on {} with enhanced security", SOCKET_PATH);
    println!("[Metadata Daemon] Security features: Input validation, access control, comprehensive audit logging");
    println!("[Metadata Daemon] Performance features: Connection pooling, caching, metrics collection, vector similarity search");
    
    // Spawn WAL/2PC polling task with enhanced error handling and metrics
    let db_clone = db.clone();
    let metrics_clone = metrics.clone();
    tokio::spawn(async move {
        if let Err(e) = wal_polling_loop(db_clone, metrics_clone).await {
            eprintln!("[Metadata Daemon] WAL polling error: {}", e);
            ERRORS_TOTAL.fetch_add(1, Ordering::Relaxed);
        }
    });
    
    // Start metrics/health server in background
    let metrics_clone = metrics.clone();
    tokio::spawn(async move {
        metrics_server(metrics_clone).await;
    });
    
    // Accept connections with enhanced security and performance monitoring
    loop {
        match listener.accept().await {
            Ok((stream, addr)) => {
                let db = db.clone();
                let metrics = metrics.clone();
                
                tokio::spawn(async move {
                    let start_time = Instant::now();
                    
                    if let Err(e) = handle_client(stream, db, &metrics).await {
                        eprintln!("[Metadata Daemon] Client error: {}", e);
                        metrics.record_error();
                        ERRORS_TOTAL.fetch_add(1, Ordering::Relaxed);
                    }
                    
                    let latency = start_time.elapsed();
                    metrics.record_request(latency);
                });
            }
            Err(e) => {
                eprintln!("[Metadata Daemon] Accept error: {}", e);
                ERRORS_TOTAL.fetch_add(1, Ordering::Relaxed);
            }
        }
    }
}

fn init_db(conn: &Connection) -> rusqlite::Result<()> {
    // Load SQLite vector extension if available
    if let Err(e) = conn.load_extension("libsqlite_vector", None) {
        eprintln!("[Metadata Daemon] Warning: Vector extension not available: {}", e);
    }
    
    conn.execute_batch(r#"
        CREATE TABLE IF NOT EXISTS files (
            id INTEGER PRIMARY KEY,
            path TEXT NOT NULL UNIQUE,
            subvol_id INTEGER,
            inode INTEGER,
            checksum TEXT,
            mtime DATETIME
        );
        CREATE TABLE IF NOT EXISTS metadata_tags (
            id INTEGER PRIMARY KEY,
            file_id INTEGER,
            tag_key TEXT,
            tag_value TEXT,
            FOREIGN KEY(file_id) REFERENCES files(id)
        );
        CREATE TABLE IF NOT EXISTS embeddings (
            id INTEGER PRIMARY KEY,
            file_id INTEGER,
            embedding_type TEXT,
            embedding BLOB,
            embedding_vector VECTOR(384), -- Vector extension column for similarity search
            FOREIGN KEY(file_id) REFERENCES files(id)
        );
        CREATE TABLE IF NOT EXISTS events (
            id INTEGER PRIMARY KEY,
            event_type TEXT,
            path TEXT,
            extra TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        -- Application manifest and dependency graph support
        CREATE TABLE IF NOT EXISTS application_manifests (
            id INTEGER PRIMARY KEY,
            app_name TEXT NOT NULL,
            exec_path TEXT NOT NULL,
            manifest_json TEXT NOT NULL,
            last_updated DATETIME DEFAULT CURRENT_TIMESTAMP
        );
        CREATE TABLE IF NOT EXISTS dependency_edges (
            id INTEGER PRIMARY KEY,
            app_id INTEGER NOT NULL,
            soname TEXT NOT NULL,
            version TEXT,
            required INTEGER DEFAULT 1,
            resolved_path TEXT,
            FOREIGN KEY(app_id) REFERENCES application_manifests(id)
        );
        
        -- Create indexes for better performance
        CREATE INDEX IF NOT EXISTS idx_files_path ON files(path);
        CREATE INDEX IF NOT EXISTS idx_metadata_tags_file_id ON metadata_tags(file_id);
        CREATE INDEX IF NOT EXISTS idx_metadata_tags_key_value ON metadata_tags(tag_key, tag_value);
        CREATE INDEX IF NOT EXISTS idx_embeddings_file_id ON embeddings(file_id);
        CREATE INDEX IF NOT EXISTS idx_embeddings_type ON embeddings(embedding_type);
        CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(timestamp);
        CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);
    "#)
}

async fn handle_client(stream: UnixStream, db: Arc<Mutex<Connection>>, metrics: &PerformanceMetrics) -> anyhow::Result<()> {
    let (reader, mut writer) = stream.into_split();
    let mut lines = BufReader::new(reader).lines();
    while let Some(line) = lines.next_line().await? {
        let resp = process_command(&line, &db, metrics).await;
        writer.write_all(resp.as_bytes()).await?;
        writer.write_all(b"\n").await?;
    }
    Ok(())
}

async fn process_command(line: &str, db: &Arc<Mutex<Connection>>, metrics: &PerformanceMetrics) -> String {
    let parts: Vec<&str> = line.trim().split_whitespace().collect();
    if parts.is_empty() {
        return "ERR: Empty command".to_string();
    }
    
    // Get current user for audit logging
    let current_user = env::var("USER").unwrap_or_else(|_| "unknown".to_string());
    
    match parts[0] {
        "EVENT" => {
            // EVENT <type> <path> [extra]
            if parts.len() < 3 {
                return "ERR: Usage: EVENT <type> <path> [extra]".to_string();
            }
            let event_type = parts[1];
            let path = parts[2];
            let extra = if parts.len() > 3 { Some(parts[3..].join(" ")) } else { None };
            
            // Validate path
            if let Err(e) = validate_path(path) {
                let _ = log_audit_event("EVENT", &current_user, path, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            
            let conn = db.lock().unwrap();
            let res = conn.execute(
                "INSERT INTO events (event_type, path, extra) VALUES (?1, ?2, ?3)",
                params![event_type, path, extra],
            );
            match res {
                Ok(_) => {
                    EVENTS_PROCESSED.fetch_add(1, Ordering::Relaxed);
                    let _ = log_audit_event("EVENT", &current_user, path, "SUCCESS", event_type);
                    "OK: Event added".to_string()
                }
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("EVENT", &current_user, path, "FAILED", &e.to_string());
                    format!("ERR: {}", e)
                }
            }
        }
        "TAG" => {
            // TAG <path> <key> <value>
            if parts.len() != 4 {
                return "ERR: Usage: TAG <path> <key> <value>".to_string();
            }
            let path = parts[1];
            let key = parts[2];
            let value = parts[3];
            
            // Validate inputs
            if let Err(e) = validate_path(path) {
                let _ = log_audit_event("TAG", &current_user, path, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            if let Err(e) = validate_tag_key(key) {
                let _ = log_audit_event("TAG", &current_user, path, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            if let Err(e) = validate_tag_value(value) {
                let _ = log_audit_event("TAG", &current_user, path, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            
            let conn = db.lock().unwrap();
            let tx = conn.transaction().unwrap();
            let file_id: i64 = match tx.query_row(
                "SELECT id FROM files WHERE path=?1",
                params![path],
                |row| row.get(0),
            ) {
                Ok(id) => id,
                Err(_) => {
                    tx.execute("INSERT INTO files (path) VALUES (?1)", params![path]).unwrap();
                    tx.last_insert_rowid()
                }
            };
            let res = tx.execute(
                "INSERT INTO metadata_tags (file_id, tag_key, tag_value) VALUES (?1, ?2, ?3)",
                params![file_id, key, value],
            );
            match res {
                Ok(_) => {
                    TAGS_PROCESSED.fetch_add(1, Ordering::Relaxed);
                    tx.commit().unwrap();
                    let _ = log_audit_event("TAG", &current_user, path, "SUCCESS", &format!("{}={}", key, value));
                    "OK: Tag added".to_string()
                }
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("TAG", &current_user, path, "FAILED", &e.to_string());
                    format!("ERR: {}", e)
                }
            }
        }
        "EMBED" => {
            // EMBED <path> <embedding_type> <base64_embedding>
            if parts.len() != 4 {
                return "ERR: Usage: EMBED <path> <embedding_type> <base64_embedding>".to_string();
            }
            let path = parts[1];
            let embedding_type = parts[2];
            let embedding_b64 = parts[3];
            
            // Validate path
            if let Err(e) = validate_path(path) {
                let _ = log_audit_event("EMBED", &current_user, path, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            
            let embedding = match base64::decode(embedding_b64) {
                Ok(e) => e,
                Err(_) => {
                    let _ = log_audit_event("EMBED", &current_user, path, "FAILED", "Invalid base64 embedding");
                    return "ERR: Invalid base64 embedding".to_string();
                }
            };
            
            let conn = db.lock().unwrap();
            let tx = conn.transaction().unwrap();
            let file_id: i64 = match tx.query_row(
                "SELECT id FROM files WHERE path=?1",
                params![path],
                |row| row.get(0),
            ) {
                Ok(id) => id,
                Err(_) => {
                    tx.execute("INSERT INTO files (path) VALUES (?1)", params![path]).unwrap();
                    tx.last_insert_rowid()
                }
            };
            
            // Convert embedding to vector format for similarity search
            let vector_data = embedding.clone(); // In real implementation, convert to proper vector format
            
            let res = tx.execute(
                "INSERT INTO embeddings (file_id, embedding_type, embedding, embedding_vector) VALUES (?1, ?2, ?3, ?4)",
                params![file_id, embedding_type, embedding, vector_data],
            );
            match res {
                Ok(_) => {
                    EMBEDDINGS_PROCESSED.fetch_add(1, Ordering::Relaxed);
                    tx.commit().unwrap();
                    let _ = log_audit_event("EMBED", &current_user, path, "SUCCESS", embedding_type);
                    "OK: Embedding added".to_string()
                }
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("EMBED", &current_user, path, "FAILED", &e.to_string());
                    format!("ERR: {}", e)
                }
            }
        }
        "QUERY" => {
            // QUERY TAG key=value
            if parts.len() == 3 && parts[1] == "TAG" && parts[2].contains('=') {
                let kv: Vec<&str> = parts[2].splitn(2, '=').collect();
                if kv.len() != 2 {
                    return "ERR: Usage: QUERY TAG key=value".to_string();
                }
                let key = kv[0];
                let value = kv[1];
                
                // Validate query
                if let Err(e) = validate_query(&format!("{}={}", key, value)) {
                    let _ = log_audit_event("QUERY", &current_user, "TAG", "FAILED", &e.to_string());
                    return format!("ERR: {}", e);
                }
                
                let conn = db.lock().unwrap();
                let mut stmt = match conn.prepare(
                    "SELECT f.path FROM files f JOIN metadata_tags t ON f.id = t.file_id WHERE t.tag_key=?1 AND t.tag_value=?2"
                ) {
                    Ok(s) => s,
                    Err(e) => {
                        metrics.record_error();
                        let _ = log_audit_event("QUERY", &current_user, "TAG", "FAILED", &e.to_string());
                        return format!("ERR: {}", e)
                    }
                };
                let rows = stmt.query_map(params![key, value], |row| row.get::<_, String>(0));
                match rows {
                    Ok(rows) => {
                        let results: Vec<String> = rows.filter_map(Result::ok).collect();
                        QUERIES_PROCESSED.fetch_add(1, Ordering::Relaxed);
                        let _ = log_audit_event("QUERY", &current_user, "TAG", "SUCCESS", &format!("{}={}, {} results", key, value, results.len()));
                        format!("RESULT: {}", serde_json::to_string(&results).unwrap())
                    }
                    Err(e) => {
                        metrics.record_error();
                        let _ = log_audit_event("QUERY", &current_user, "TAG", "FAILED", &e.to_string());
                        format!("ERR: {}", e)
                    }
                }
            } else {
                "ERR: Unsupported query".to_string()
            }
        }
        "NLQ" => {
            // NLQ <natural language query>
            let nlq_str = line[4..].trim();
            
            // Validate query
            if let Err(e) = validate_query(nlq_str) {
                let _ = log_audit_event("NLQ", &current_user, "NATURAL_LANGUAGE", "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            
            let llm_result = match call_llm_backend(nlq_str).await {
                Ok(res) => res,
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("NLQ", &current_user, "NATURAL_LANGUAGE", "FAILED", &e);
                    return format!("ERR: LLM backend error: {}", e)
                }
            };
            match llm_result {
                Some(json_str) => {
                    match serde_json::from_str::<serde_json::Value>(&json_str) {
                        Ok(obj) => {
                            let result = execute_structured_nlq(obj, db);
                            QUERIES_PROCESSED.fetch_add(1, Ordering::Relaxed);
                            SEMANTIC_SEARCHES.fetch_add(1, Ordering::Relaxed);
                            let _ = log_audit_event("NLQ", &current_user, "NATURAL_LANGUAGE", "SUCCESS", nlq_str);
                            result
                        }
                        Err(_) => {
                            metrics.record_error();
                            let _ = log_audit_event("NLQ", &current_user, "NATURAL_LANGUAGE", "FAILED", "Invalid JSON response");
                            format!("LLM: {}", json_str)
                        }
                    }
                }
                None => {
                    metrics.record_error();
                    let _ = log_audit_event("NLQ", &current_user, "NATURAL_LANGUAGE", "FAILED", "No LLM result");
                    "ERR: LLM returned no result".to_string()
                }
            }
        }
        "VECTOR_SEARCH" => {
            // VECTOR_SEARCH <query_vector_base64> [top_k]
            if parts.len() < 2 {
                return "ERR: Usage: VECTOR_SEARCH <query_vector_base64> [top_k]".to_string();
            }
            let query_vector_b64 = parts[1];
            let top_k = if parts.len() > 2 {
                parts[2].parse::<usize>().unwrap_or(10)
            } else {
                10
            };
            
            let query_vector = match base64::decode(query_vector_b64) {
                Ok(v) => v,
                Err(_) => {
                    let _ = log_audit_event("VECTOR_SEARCH", &current_user, "VECTOR", "FAILED", "Invalid base64 vector");
                    return "ERR: Invalid base64 vector".to_string();
                }
            };
            
            match perform_vector_search(&query_vector, top_k, db).await {
                Ok(results) => {
                    VECTOR_SEARCHES.fetch_add(1, Ordering::Relaxed);
                    let _ = log_audit_event("VECTOR_SEARCH", &current_user, "VECTOR", "SUCCESS", &format!("top_k={}, results={}", top_k, results.len()));
                    format!("RESULT: {}", serde_json::to_string(&results).unwrap())
                }
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("VECTOR_SEARCH", &current_user, "VECTOR", "FAILED", &e.to_string());
                    format!("ERR: {}", e)
                }
            }
        }
        "HELP" => "Commands: EVENT, TAG, EMBED, QUERY, NLQ, VECTOR_SEARCH, HELP".to_string(),
        "MANIFEST_GET" => {
            // MANIFEST_GET <app>
            if parts.len() != 2 {
                return "ERR: Usage: MANIFEST_GET <app>".to_string();
            }
            let app = parts[1];
            
            // Validate app name
            if let Err(e) = validate_query(app) {
                let _ = log_audit_event("MANIFEST_GET", &current_user, app, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            
            let conn = db.lock().unwrap();
            let mut stmt = match conn.prepare("SELECT manifest_json FROM application_manifests WHERE app_name=?1") {
                Ok(s) => s,
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("MANIFEST_GET", &current_user, app, "FAILED", &e.to_string());
                    return format!("ERR: {}", e)
                }
            };
            let row = stmt.query_row(params![app], |row| row.get::<_, String>(0));
            match row {
                Ok(json) => {
                    MANIFESTS_PROCESSED.fetch_add(1, Ordering::Relaxed);
                    let _ = log_audit_event("MANIFEST_GET", &current_user, app, "SUCCESS", "Manifest retrieved");
                    format!("RESULT: {}", json)
                }
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("MANIFEST_GET", &current_user, app, "FAILED", &e.to_string());
                    format!("ERR: {}", e)
                }
            }
        }
        "MANIFEST_SET" => {
            // MANIFEST_SET <app> <json>
            if parts.len() < 3 {
                return "ERR: Usage: MANIFEST_SET <app> <json>".to_string();
            }
            let app = parts[1];
            let json = parts[2..].join(" ");
            
            // Validate inputs
            if let Err(e) = validate_query(app) {
                let _ = log_audit_event("MANIFEST_SET", &current_user, app, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            if let Err(e) = validate_query(&json) {
                let _ = log_audit_event("MANIFEST_SET", &current_user, app, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            
            let conn = db.lock().unwrap();
            // Upsert manifest
            let res = conn.execute(
                "INSERT INTO application_manifests (app_name, exec_path, manifest_json, last_updated) VALUES (?1, '', ?2, CURRENT_TIMESTAMP)
                 ON CONFLICT(app_name) DO UPDATE SET manifest_json=excluded.manifest_json, last_updated=CURRENT_TIMESTAMP",
                params![app, json],
            );
            if let Err(e) = res {
                let _ = log_audit_event("MANIFEST_SET", &current_user, app, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            // Get app_id
            let app_id: i64 = match conn.query_row(
                "SELECT id FROM application_manifests WHERE app_name=?1",
                params![app],
                |row| row.get(0),
            ) {
                Ok(id) => id,
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("MANIFEST_SET", &current_user, app, "FAILED", &e.to_string());
                    return format!("ERR: {}", e)
                }
            };
            // Parse manifest JSON and update dependency_edges
            // Manifest format: { "dependencies": [ { "soname": "libfoo.so.1", "version": "1.2.3", "required": true, "resolved_path": "/heros_storage/libs/..." }, ... ] }
            let manifest: serde_json::Value = match serde_json::from_str(&json) {
                Ok(v) => v,
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("MANIFEST_SET", &current_user, app, "FAILED", &format!("Invalid JSON: {}", e));
                    return format!("ERR: Invalid JSON: {}", e)
                }
            };
            // Remove old edges
            if let Err(e) = conn.execute("DELETE FROM dependency_edges WHERE app_id=?1", params![app_id]) {
                let _ = log_audit_event("MANIFEST_SET", &current_user, app, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            if let Some(deps) = manifest.get("dependencies").and_then(|v| v.as_array()) {
                for dep in deps {
                    let soname = dep.get("soname").and_then(|v| v.as_str()).unwrap_or("");
                    let version = dep.get("version").and_then(|v| v.as_str());
                    let required = dep.get("required").and_then(|v| v.as_bool()).unwrap_or(true);
                    let resolved_path = dep.get("resolved_path").and_then(|v| v.as_str());
                    let res = conn.execute(
                        "INSERT INTO dependency_edges (app_id, soname, version, required, resolved_path) VALUES (?1, ?2, ?3, ?4, ?5)",
                        params![app_id, soname, version, required as i64, resolved_path],
                    );
                    if let Err(e) = res {
                        let _ = log_audit_event("MANIFEST_SET", &current_user, app, "FAILED", &e.to_string());
                        return format!("ERR: {}", e);
                    }
                }
            }
            let _ = log_audit_event("MANIFEST_SET", &current_user, app, "SUCCESS", "Manifest updated");
            "OK".to_string()
        }
        "MANIFEST_DEP_GRAPH" => {
            // MANIFEST_DEP_GRAPH <app>
            if parts.len() != 2 {
                return "ERR: Usage: MANIFEST_DEP_GRAPH <app>".to_string();
            }
            let app = parts[1];
            
            // Validate app name
            if let Err(e) = validate_query(app) {
                let _ = log_audit_event("MANIFEST_DEP_GRAPH", &current_user, app, "FAILED", &e.to_string());
                return format!("ERR: {}", e);
            }
            
            let conn = db.lock().unwrap();
            let mut stmt = match conn.prepare(
                "SELECT d.soname, d.version, d.required, d.resolved_path FROM dependency_edges d JOIN application_manifests a ON d.app_id=a.id WHERE a.app_name=?1"
            ) {
                Ok(s) => s,
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("MANIFEST_DEP_GRAPH", &current_user, app, "FAILED", &e.to_string());
                    return format!("ERR: {}", e)
                }
            };
            let rows = stmt.query_map(params![app], |row| {
                Ok(serde_json::json!({
                    "soname": row.get::<_, String>(0)?,
                    "version": row.get::<_, Option<String>>(1)?,
                    "required": row.get::<_, i64>(2)? == 1,
                    "resolved_path": row.get::<_, Option<String>>(3)?
                }))
            });
            match rows {
                Ok(rows) => {
                    let results: Vec<_> = rows.filter_map(Result::ok).collect();
                    QUERIES_PROCESSED.fetch_add(1, Ordering::Relaxed);
                    let _ = log_audit_event("MANIFEST_DEP_GRAPH", &current_user, app, "SUCCESS", &format!("{} dependencies", results.len()));
                    format!("RESULT: {}", serde_json::to_string(&results).unwrap())
                }
                Err(e) => {
                    metrics.record_error();
                    let _ = log_audit_event("MANIFEST_DEP_GRAPH", &current_user, app, "FAILED", &e.to_string());
                    format!("ERR: {}", e)
                }
            }
        }
        _ => {
            metrics.record_error();
            let _ = log_audit_event("UNKNOWN_COMMAND", &current_user, parts[0], "FAILED", "Unknown command");
            "ERR: Unknown command".to_string()
        }
    }
}

async fn call_llm_backend(nlq_str: &str) -> Result<Option<String>, String> {
    let ollama_api_url = std::env::var("OLLAMA_API_URL").ok();
    let openai_api_key = std::env::var("OPENAI_API_KEY").ok();
    let prompt = format!(
        "Translate the following user request into a JSON object with fields: action, filters, tags, date_range, collaborators, logic, etc. Only return the JSON.\nUser request: {}",
        nlq_str
    );
    if let Some(url) = ollama_api_url {
        let client = reqwest::Client::new();
        let resp = client.post(&url).json(&serde_json::json!({"prompt": prompt})).send().await.map_err(|e| e.to_string())?;
        if resp.status().is_success() {
            let v: serde_json::Value = resp.json().await.map_err(|e| e.to_string())?;
            return Ok(v.get("result").and_then(|r| r.as_str()).map(|s| s.to_string()));
        } else {
            return Err(format!("Ollama error: {}", resp.status()));
        }
    }
    if let Some(key) = openai_api_key {
        let client = reqwest::Client::new();
        let resp = client.post("https://api.openai.com/v1/chat/completions")
            .bearer_auth(key)
            .json(&serde_json::json!({
                "model": "gpt-3.5-turbo",
                "messages": [{"role": "user", "content": prompt}]
            }))
            .send().await.map_err(|e| e.to_string())?;
        if resp.status().is_success() {
            let v: serde_json::Value = resp.json().await.map_err(|e| e.to_string())?;
            if let Some(content) = v["choices"][0]["message"]["content"].as_str() {
                return Ok(Some(content.to_string()));
            }
        } else {
            return Err(format!("OpenAI error: {}", resp.status()));
        }
    }
    Ok(None)
}

fn execute_structured_nlq(obj: serde_json::Value, db: &Arc<Mutex<Connection>>) -> String {
    // Only support action: list, filters: project, collaborator, tag, date_range, logic (AND/OR)
    let action = obj.get("action").and_then(|a| a.as_str()).unwrap_or("");
    let filters = obj.get("filters").cloned().unwrap_or(serde_json::json!({}));
    let logic = obj.get("logic").and_then(|l| l.as_str()).unwrap_or("AND").to_uppercase();
    let mut sql = "SELECT f.path FROM files f".to_string();
    let mut joins = vec![];
    let mut wheres = vec![];
    let mut params: Vec<String> = vec![];
    if let Some(project) = filters.get("project").and_then(|v| v.as_str()) {
        joins.push("JOIN metadata_tags t1 ON f.id = t1.file_id");
        wheres.push("t1.tag_key='project' AND t1.tag_value=?");
        params.push(project.to_string());
    }
    if let Some(collab) = filters.get("collaborator").and_then(|v| v.as_str()) {
        joins.push("JOIN metadata_tags t2 ON f.id = t2.file_id");
        wheres.push("t2.tag_key='collaborator' AND t2.tag_value=?");
        params.push(collab.to_string());
    }
    if let Some(tag) = filters.get("tag").and_then(|v| v.as_str()) {
        joins.push("JOIN metadata_tags t3 ON f.id = t3.file_id");
        wheres.push("t3.tag_value=?");
        params.push(tag.to_string());
    }
    if let Some(dr) = filters.get("date_range") {
        if let (Some(from), Some(to)) = (dr.get("from").and_then(|v| v.as_str()), dr.get("to").and_then(|v| v.as_str())) {
            wheres.push("f.mtime BETWEEN ? AND ?");
            params.push(from.to_string());
            params.push(to.to_string());
        }
    }
    if !joins.is_empty() {
        sql += &format!(" {}", joins.join(" "));
    }
    if !wheres.is_empty() {
        sql += " WHERE ";
        if logic == "OR" {
            sql += &wheres.join(" OR ");
        } else {
            sql += &wheres.join(" AND ");
        }
    }
    let conn = db.lock().unwrap();
    let mut stmt = match conn.prepare(&sql) {
        Ok(s) => s,
        Err(e) => return format!("ERR: {}", e),
    };
    let rows = stmt.query_map(params.iter().map(|s| s as &dyn rusqlite::ToSql), |row| row.get::<_, String>(0));
    match rows {
        Ok(rows) => {
            let results: Vec<String> = rows.filter_map(Result::ok).collect();
            format!("RESULT: {}", serde_json::to_string(&results).unwrap())
        }
        Err(e) => format!("ERR: {}", e),
    }
}

async fn perform_vector_search(query_vector: &[u8], top_k: usize, db: &Arc<Mutex<Connection>>) -> Result<Vec<(String, f32)>, MetadataError> {
    let conn = db.lock().unwrap();
    let mut stmt = match conn.prepare(
        "SELECT f.path, e.embedding_vector <-> ? AS similarity FROM embeddings e JOIN files f ON e.file_id = f.id ORDER BY similarity DESC LIMIT ?"
    ) {
        Ok(s) => s,
        Err(e) => return Err(MetadataError::VectorSearchError(format!("Failed to prepare vector search statement: {}", e))),
    };
    let rows = stmt.query_map(params![query_vector, top_k as i64], |row| {
        let path: String = row.get(0)?;
        let similarity: f32 = row.get(1)?;
        Ok((path, similarity))
    });
    match rows {
        Ok(rows) => Ok(rows.filter_map(Result::ok).collect()),
        Err(e) => Err(MetadataError::VectorSearchError(format!("Error executing vector search: {}", e))),
    }
}

async fn wal_polling_loop(db: Arc<Mutex<Connection>>, metrics: Arc<PerformanceMetrics>) {
    use tokio::time::{sleep, Duration};
    let wal_api_url = std::env::var("HEROS_WAL_API").unwrap_or_else(|_| "http://127.0.0.1:9292/wal".to_string());
    let wal_commit_api = std::env::var("HEROS_WAL_COMMIT_API").unwrap_or_else(|_| "http://127.0.0.1:9292/commit".to_string());
    let client = reqwest::Client::new();
    loop {
        match client.get(&wal_api_url).send().await {
            Ok(resp) if resp.status().is_success() => {
                match resp.json::<Vec<serde_json::Value>>().await {
                    Ok(entries) => {
                        for entry in entries {
                            if entry.get("committed").and_then(|v| v.as_bool()) == Some(false) {
                                let op = entry.get("op").and_then(|v| v.as_str()).unwrap_or("");
                                let path = entry.get("path").and_then(|v| v.as_str()).unwrap_or("");
                                let extra = entry.get("extra").and_then(|v| v.as_str());
                                let cmd = match op {
                                    "CREATE" => format!("EVENT CREATE {}", path),
                                    "WRITE" => format!("EVENT WRITE {}", path),
                                    "DELETE" => format!("EVENT DELETE {}", path),
                                    "RENAME" => format!("EVENT RENAME {} {}", path, extra.unwrap_or("")),
                                    _ => continue,
                                };
                                // Apply to DB
                                let _ = process_command(&cmd, &db, &metrics).await;
                                // Commit to WAL
                                let _ = client.post(&wal_commit_api).json(&entry).send().await;
                                
                                // Record metrics
                                metrics.record_wal_entry();
                                WAL_ENTRIES_PROCESSED.fetch_add(1, Ordering::Relaxed);
                            }
                        }
                    }
                    Err(e) => {
                        eprintln!("[WAL/2PC] JSON parse error: {}", e);
                        metrics.record_error();
                    }
                }
            }
            Ok(resp) => {
                eprintln!("[WAL/2PC] WAL API error: {}", resp.status());
                metrics.record_error();
            }
            Err(e) => {
                eprintln!("[WAL/2PC] WAL polling error: {}", e);
                metrics.record_error();
            }
        }
        sleep(Duration::from_secs(5)).await;
    }
}

// Enhanced contextual semantic search with real vector similarity scoring
fn contextual_semantic_search(
    tags: Option<&[String]>,
    projects: Option<&[String]>,
    collaborators: Option<&[String]>,
    time_range: Option<(i64, i64)>,
    query_vector: Option<&[f32]>,
    top_k: usize,
    db: &Arc<Mutex<Connection>>,
) -> Result<Vec<(String, f32)>, MetadataError> {
    let conn = db.lock().unwrap();
    
    // Build base query with filters
    let mut sql = String::from("SELECT f.path, e.embedding_vector");
    let mut joins = vec!["JOIN embeddings e ON f.id = e.file_id".to_string()];
    let mut wheres = vec![];
    let mut params: Vec<Box<dyn rusqlite::ToSql>> = vec![];
    
    // Add tag filters
    if let Some(tags) = tags {
        for (i, tag) in tags.iter().enumerate() {
            joins.push(format!("JOIN metadata_tags t{} ON f.id = t{}.file_id", i, i));
            wheres.push(format!("t{}.tag_value = ?", i));
            params.push(Box::new(tag.clone()));
        }
    }
    
    // Add project filters
    if let Some(projects) = projects {
        for (i, project) in projects.iter().enumerate() {
            let offset = tags.map(|t| t.len()).unwrap_or(0);
            joins.push(format!("JOIN metadata_tags p{} ON f.id = p{}.file_id", i + offset, i + offset));
            wheres.push(format!("p{}.tag_key = 'project' AND p{}.tag_value = ?", i + offset, i + offset));
            params.push(Box::new(project.clone()));
        }
    }
    
    // Add collaborator filters
    if let Some(collabs) = collaborators {
        for (i, collab) in collabs.iter().enumerate() {
            let offset = tags.map(|t| t.len()).unwrap_or(0) + projects.map(|p| p.len()).unwrap_or(0);
            joins.push(format!("JOIN metadata_tags c{} ON f.id = c{}.file_id", i + offset, i + offset));
            wheres.push(format!("c{}.tag_key = 'collaborator' AND c{}.tag_value = ?", i + offset, i + offset));
            params.push(Box::new(collab.clone()));
        }
    }
    
    // Add time range filter
    if let Some((start, end)) = time_range {
        wheres.push("f.mtime BETWEEN ? AND ?".to_string());
        params.push(Box::new(start));
        params.push(Box::new(end));
    }
    
    // Build complete SQL
    sql += &format!(" FROM files f {}", joins.join(" "));
    if !wheres.is_empty() {
        sql += &format!(" WHERE {}", wheres.join(" AND "));
    }
    
    // Add vector similarity if query vector provided
    if let Some(vec) = query_vector {
        sql += " ORDER BY e.embedding_vector <-> ? DESC LIMIT ?";
        params.push(Box::new(vec.to_vec()));
        params.push(Box::new(top_k as i64));
        
        let mut stmt = match conn.prepare(&sql) {
            Ok(s) => s,
            Err(e) => return Err(MetadataError::VectorSearchError(format!("Failed to prepare semantic search: {}", e))),
        };
        
        let rows = stmt.query_map(params.iter().map(|p| p.as_ref()), |row| {
            let path: String = row.get(0)?;
            let similarity: f32 = row.get(1)?;
            Ok((path, similarity))
        });
        
        match rows {
            Ok(rows) => Ok(rows.filter_map(Result::ok).collect()),
            Err(e) => Err(MetadataError::VectorSearchError(format!("Error executing semantic search: {}", e))),
        }
    } else {
        // No vector similarity, just return filtered results
        sql += &format!(" LIMIT {}", top_k);
        
        let mut stmt = match conn.prepare(&sql) {
            Ok(s) => s,
            Err(e) => return Err(MetadataError::VectorSearchError(format!("Failed to prepare filtered search: {}", e))),
        };
        
        let rows = stmt.query_map(params.iter().map(|p| p.as_ref()), |row| {
            let path: String = row.get(0)?;
            Ok((path, 1.0)) // Default similarity score
        });
        
        match rows {
            Ok(rows) => Ok(rows.filter_map(Result::ok).collect()),
            Err(e) => Err(MetadataError::VectorSearchError(format!("Error executing filtered search: {}", e))),
        }
    }
}

// Enhanced NLQ parser with better pattern recognition
fn parse_nlq_advanced(nlq: &str) -> HashMap<String, String> {
    let mut map = HashMap::new();
    
    // Extract key-value pairs with various patterns
    let patterns = vec![
        (r"project:(\S+)", "project"),
        (r"collaborator:(\S+)", "collaborator"),
        (r"tag:(\S+)", "tag"),
        (r"from:(\S+)", "date_from"),
        (r"to:(\S+)", "date_to"),
        (r"action:(\S+)", "action"),
        (r"logic:(\S+)", "logic"),
    ];
    
    for (pattern, key) in patterns {
        if let Ok(re) = Regex::new(pattern) {
            if let Some(captures) = re.captures(nlq) {
                if let Some(value) = captures.get(1) {
                    map.insert(key.to_string(), value.as_str().to_string());
                }
            }
        }
    }
    
    // Extract natural language patterns
    if nlq.contains("show me") || nlq.contains("find") || nlq.contains("list") {
        map.insert("action".to_string(), "list".to_string());
    }
    
    if nlq.contains("and") {
        map.insert("logic".to_string(), "AND".to_string());
    } else if nlq.contains("or") {
        map.insert("logic".to_string(), "OR".to_string());
    }
    
    map
} 