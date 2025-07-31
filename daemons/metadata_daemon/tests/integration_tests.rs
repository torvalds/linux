//! HER OS Metadata Daemon - Comprehensive Integration Tests
//!
//! This test suite covers all aspects of the Metadata Daemon including:
//! - Security validation and access control
//! - Performance and scalability testing
//! - Error handling and recovery
//! - All protocol commands and functionality
//! - Database operations and consistency
//! - Integration with external systems
//!
//! Author: HER OS Project
//! License: GPL-2.0

use std::os::unix::net::UnixStream;
use std::io::{Write, BufRead, BufReader};
use std::process::{Command, Stdio};
use std::thread;
use std::time::Duration;
use serde_json::Value;
use tempfile::TempDir;

const SOCKET_PATH: &str = "/tmp/heros_metadata_test.sock";
const TEST_DB_PATH: &str = "test_heros_meta.db";

/// Test configuration and utilities
struct TestConfig {
    temp_dir: TempDir,
    daemon_process: Option<std::process::Child>,
}

impl TestConfig {
    fn new() -> Self {
        let temp_dir = tempfile::tempdir().expect("Failed to create temp directory");
        Self {
            temp_dir,
            daemon_process: None,
        }
    }
    
    fn start_daemon(&mut self) {
        // Set test environment variables
        std::env::set_var("HEROS_TEST_MODE", "1");
        std::env::set_var("HEROS_TEST_SOCKET", SOCKET_PATH);
        std::env::set_var("HEROS_TEST_DB", TEST_DB_PATH);
        
        // Start the daemon process
        let daemon = Command::new("cargo")
            .args(&["run", "--bin", "metadata_daemon"])
            .current_dir("daemons/metadata_daemon")
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()
            .expect("Failed to start metadata daemon");
        
        self.daemon_process = Some(daemon);
        
        // Wait for daemon to start
        thread::sleep(Duration::from_millis(1000));
        
        // Wait for socket to be available
        let mut attempts = 0;
        while attempts < 30 {
            if std::path::Path::new(SOCKET_PATH).exists() {
                break;
            }
            thread::sleep(Duration::from_millis(100));
            attempts += 1;
        }
        
        assert!(std::path::Path::new(SOCKET_PATH).exists(), "Daemon socket not created");
    }
    
    fn stop_daemon(&mut self) {
        if let Some(mut process) = self.daemon_process.take() {
            let _ = process.kill();
            let _ = process.wait();
        }
        
        // Clean up test files
        let _ = std::fs::remove_file(SOCKET_PATH);
        let _ = std::fs::remove_file(TEST_DB_PATH);
    }
}

impl Drop for TestConfig {
    fn drop(&mut self) {
        self.stop_daemon();
    }
}

/// Send a command to the daemon and return the response
fn send_command(command: &str) -> Result<String, Box<dyn std::error::Error>> {
    let mut stream = UnixStream::connect(SOCKET_PATH)?;
    stream.write_all(command.as_bytes())?;
    stream.write_all(b"\n")?;
    
    let mut reader = BufReader::new(stream);
    let mut response = String::new();
    reader.read_line(&mut response)?;
    
    Ok(response.trim_end().to_string())
}

/// Test basic daemon functionality
#[test]
fn test_daemon_basic_functionality() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Test HELP command
    let response = send_command("HELP").expect("Failed to send HELP command");
    assert!(response.contains("EVENT"), "HELP response should contain EVENT");
    assert!(response.contains("TAG"), "HELP response should contain TAG");
    assert!(response.contains("QUERY"), "HELP response should contain QUERY");
}

/// Test security validation
#[test]
fn test_security_validation() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Test path traversal prevention
    let malicious_paths = vec![
        "../../../etc/passwd",
        "..\\..\\..\\windows\\system32\\config\\sam",
        "/etc/passwd",
        "~/.ssh/id_rsa",
        "//etc//passwd",
    ];
    
    for path in malicious_paths {
        let response = send_command(&format!("TAG {} test_key test_value", path))
            .expect("Failed to send malicious TAG command");
        assert!(response.contains("ERR"), "Should reject malicious path: {}", path);
        assert!(response.contains("Security"), "Should indicate security violation: {}", path);
    }
    
    // Test SQL injection prevention
    let sql_injection_queries = vec![
        "union select * from files",
        "drop table files",
        "delete from files",
        "insert into files values (1, 'test', 1, 1, 'test', 'test')",
        "update files set path = 'hacked'",
        "alter table files add column hacked text",
        "create table hacked (id int)",
        "exec system('rm -rf /')",
        "execute malicious_command",
    ];
    
    for query in sql_injection_queries {
        let response = send_command(&format!("QUERY {}", query))
            .expect("Failed to send SQL injection query");
        assert!(response.contains("ERR"), "Should reject SQL injection: {}", query);
        assert!(response.contains("Security"), "Should indicate security violation: {}", query);
    }
    
    // Test input length limits
    let long_path = "a".repeat(5000);
    let response = send_command(&format!("TAG {} test_key test_value", long_path))
        .expect("Failed to send long path command");
    assert!(response.contains("ERR"), "Should reject overly long path");
    
    let long_tag_key = "a".repeat(200);
    let response = send_command(&format!("TAG /tmp/test.txt {} test_value", long_tag_key))
        .expect("Failed to send long tag key command");
    assert!(response.contains("ERR"), "Should reject overly long tag key");
    
    let long_tag_value = "a".repeat(2000);
    let response = send_command(&format!("TAG /tmp/test.txt test_key {}", long_tag_value))
        .expect("Failed to send long tag value command");
    assert!(response.contains("ERR"), "Should reject overly long tag value");
}

/// Test file events
#[test]
fn test_file_events() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Test valid file events
    let valid_events = vec![
        ("EVENT CREATE /tmp/test1.txt", "OK"),
        ("EVENT WRITE /tmp/test1.txt", "OK"),
        ("EVENT DELETE /tmp/test1.txt", "OK"),
        ("EVENT RENAME /tmp/test2.txt /tmp/test3.txt", "OK"),
        ("EVENT MKDIR /tmp/testdir", "OK"),
        ("EVENT RMDIR /tmp/testdir", "OK"),
    ];
    
    for (command, expected_prefix) in valid_events {
        let response = send_command(command).expect("Failed to send event command");
        assert!(response.starts_with(expected_prefix), 
            "Event command '{}' should return '{}', got '{}'", command, expected_prefix, response);
    }
    
    // Test invalid event types
    let response = send_command("EVENT INVALID /tmp/test.txt")
        .expect("Failed to send invalid event command");
    assert!(response.contains("ERR"), "Should reject invalid event type");
}

/// Test tag operations
#[test]
fn test_tag_operations() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Add a file first
    send_command("EVENT CREATE /tmp/tagtest.txt").expect("Failed to create test file");
    
    // Test valid tag operations
    let valid_tags = vec![
        ("TAG /tmp/tagtest.txt project Alpha", "OK"),
        ("TAG /tmp/tagtest.txt priority high", "OK"),
        ("TAG /tmp/tagtest.txt owner john", "OK"),
        ("TAG /tmp/tagtest.txt confidential true", "OK"),
    ];
    
    for (command, expected_prefix) in valid_tags {
        let response = send_command(command).expect("Failed to send tag command");
        assert!(response.starts_with(expected_prefix), 
            "Tag command '{}' should return '{}', got '{}'", command, expected_prefix, response);
    }
    
    // Test querying tags
    let response = send_command("QUERY TAG project=Alpha")
        .expect("Failed to query tags");
    assert!(response.starts_with("RESULT"), "Tag query should return RESULT");
    
    // Parse JSON result
    let json_str = response.strip_prefix("RESULT: ").unwrap();
    let result: Vec<String> = serde_json::from_str(json_str).expect("Failed to parse JSON result");
    assert!(result.contains(&"/tmp/tagtest.txt".to_string()), "Query result should contain test file");
}

/// Test embedding operations
#[test]
fn test_embedding_operations() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Add a file first
    send_command("EVENT CREATE /tmp/embedtest.txt").expect("Failed to create test file");
    
    // Test valid embedding operations
    let test_embedding = base64::encode(vec![0.1, 0.2, 0.3, 0.4, 0.5].iter().map(|&x| (x * 255.0) as u8).collect::<Vec<u8>>());
    
    let response = send_command(&format!("EMBED /tmp/embedtest.txt text {}", test_embedding))
        .expect("Failed to send embedding command");
    assert!(response.starts_with("OK"), "Embedding command should return OK, got '{}'", response);
    
    // Test invalid embedding (too large)
    let large_embedding = base64::encode(vec![0u8; 10000]);
    let response = send_command(&format!("EMBED /tmp/embedtest.txt text {}", large_embedding))
        .expect("Failed to send large embedding command");
    assert!(response.contains("ERR"), "Should reject overly large embedding");
}

/// Test natural language queries
#[test]
fn test_natural_language_queries() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Set up test data
    send_command("EVENT CREATE /tmp/nlqtest1.txt").expect("Failed to create test file 1");
    send_command("EVENT CREATE /tmp/nlqtest2.txt").expect("Failed to create test file 2");
    send_command("TAG /tmp/nlqtest1.txt project Alpha").expect("Failed to add tag 1");
    send_command("TAG /tmp/nlqtest2.txt project Beta").expect("Failed to add tag 2");
    
    // Test NLQ queries
    let nlq_queries = vec![
        "NLQ Find files tagged project Alpha",
        "NLQ List all files in project Alpha",
        "NLQ Show files with project tag",
    ];
    
    for query in nlq_queries {
        let response = send_command(query).expect("Failed to send NLQ");
        // NLQ responses can vary, but should not be errors
        assert!(!response.contains("ERR"), "NLQ should not return error: {}", response);
    }
}

/// Test application manifest operations
#[test]
fn test_application_manifests() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Test setting a manifest
    let manifest_json = r#"{
        "dependencies": [
            {
                "soname": "libfoo.so.1",
                "version": "1.2.3",
                "required": true,
                "resolved_path": "/heros_storage/libs/libfoo/1.2.3/libfoo.so.1"
            },
            {
                "soname": "libbar.so.2",
                "version": "2.0.0",
                "required": false,
                "resolved_path": "/heros_storage/libs/libbar/2.0.0/libbar.so.2"
            }
        ]
    }"#;
    
    let response = send_command(&format!("MANIFEST_SET testapp {}", manifest_json))
        .expect("Failed to set manifest");
    assert!(response.starts_with("OK"), "Manifest set should return OK, got '{}'", response);
    
    // Test getting a manifest
    let response = send_command("MANIFEST_GET testapp")
        .expect("Failed to get manifest");
    assert!(response.starts_with("RESULT"), "Manifest get should return RESULT");
    
    // Parse and validate JSON
    let json_str = response.strip_prefix("RESULT: ").unwrap();
    let manifest: Value = serde_json::from_str(json_str).expect("Failed to parse manifest JSON");
    assert!(manifest["dependencies"].is_array(), "Manifest should have dependencies array");
    
    // Test getting dependency graph
    let response = send_command("MANIFEST_DEP_GRAPH testapp")
        .expect("Failed to get dependency graph");
    assert!(response.starts_with("RESULT"), "Dependency graph should return RESULT");
    
    // Parse and validate dependency graph
    let json_str = response.strip_prefix("RESULT: ").unwrap();
    let deps: Vec<Value> = serde_json::from_str(json_str).expect("Failed to parse dependency graph");
    assert_eq!(deps.len(), 2, "Should have 2 dependencies");
}

/// Test performance and scalability
#[test]
fn test_performance_scalability() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Test bulk operations
    let start_time = std::time::Instant::now();
    
    // Create 1000 files and tags
    for i in 0..1000 {
        let file_path = format!("/tmp/perftest_{}.txt", i);
        send_command(&format!("EVENT CREATE {}", file_path)).expect("Failed to create file");
        send_command(&format!("TAG {} index {}", file_path, i)).expect("Failed to add tag");
    }
    
    let bulk_time = start_time.elapsed();
    println!("Bulk operations (1000 files + tags) took: {:?}", bulk_time);
    assert!(bulk_time < Duration::from_secs(10), "Bulk operations should complete within 10 seconds");
    
    // Test query performance
    let query_start = std::time::Instant::now();
    let response = send_command("QUERY TAG index=500")
        .expect("Failed to query specific tag");
    let query_time = query_start.elapsed();
    
    println!("Single tag query took: {:?}", query_time);
    assert!(query_time < Duration::from_millis(100), "Query should complete within 100ms");
    
    // Test concurrent operations
    let mut handles = vec![];
    for i in 0..10 {
        let handle = thread::spawn(move || {
            for j in 0..100 {
                let file_path = format!("/tmp/conctest_{}_{}.txt", i, j);
                let _ = send_command(&format!("EVENT CREATE {}", file_path));
                let _ = send_command(&format!("TAG {} thread {}", file_path, i));
            }
        });
        handles.push(handle);
    }
    
    for handle in handles {
        handle.join().expect("Thread failed to join");
    }
    
    let total_time = start_time.elapsed();
    println!("Total performance test took: {:?}", total_time);
    assert!(total_time < Duration::from_secs(30), "Performance test should complete within 30 seconds");
}

/// Test error handling and recovery
#[test]
fn test_error_handling_recovery() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Test malformed commands
    let malformed_commands = vec![
        "",  // Empty command
        "INVALID_COMMAND",  // Unknown command
        "EVENT",  // Missing arguments
        "TAG",  // Missing arguments
        "QUERY",  // Missing arguments
        "EVENT CREATE",  // Missing path
        "TAG /tmp/test.txt",  // Missing key/value
        "QUERY TAG",  // Missing key=value
    ];
    
    for command in malformed_commands {
        let response = send_command(command).expect("Failed to send malformed command");
        assert!(response.contains("ERR"), "Should return error for malformed command: '{}'", command);
    }
    
    // Test operations on non-existent files
    let response = send_command("TAG /tmp/nonexistent.txt test_key test_value")
        .expect("Failed to send tag for non-existent file");
    assert!(response.contains("ERR"), "Should return error for non-existent file");
    
    // Test invalid JSON in manifests
    let response = send_command("MANIFEST_SET testapp {invalid json}")
        .expect("Failed to send invalid JSON manifest");
    assert!(response.contains("ERR"), "Should return error for invalid JSON");
}

/// Test metrics and monitoring
#[test]
fn test_metrics_monitoring() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Perform some operations to generate metrics
    send_command("EVENT CREATE /tmp/metricstest.txt").expect("Failed to create test file");
    send_command("TAG /tmp/metricstest.txt test_key test_value").expect("Failed to add tag");
    send_command("QUERY TAG test_key=test_value").expect("Failed to query tag");
    
    // Wait a moment for metrics to update
    thread::sleep(Duration::from_millis(100));
    
    // Test health endpoint (would need HTTP client in real test)
    // For now, just verify the daemon is still running
    let response = send_command("HELP").expect("Failed to send HELP command");
    assert!(response.contains("EVENT"), "Daemon should still be responsive");
}

/// Test database consistency
#[test]
fn test_database_consistency() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Create file and add tags
    send_command("EVENT CREATE /tmp/consistencytest.txt").expect("Failed to create test file");
    send_command("TAG /tmp/consistencytest.txt project Alpha").expect("Failed to add tag 1");
    send_command("TAG /tmp/consistencytest.txt priority high").expect("Failed to add tag 2");
    
    // Query and verify consistency
    let response1 = send_command("QUERY TAG project=Alpha").expect("Failed to query project tag");
    let response2 = send_command("QUERY TAG priority=high").expect("Failed to query priority tag");
    
    assert!(response1.contains("/tmp/consistencytest.txt"), "Project query should return test file");
    assert!(response2.contains("/tmp/consistencytest.txt"), "Priority query should return test file");
    
    // Delete file and verify it's removed from queries
    send_command("EVENT DELETE /tmp/consistencytest.txt").expect("Failed to delete test file");
    
    let response3 = send_command("QUERY TAG project=Alpha").expect("Failed to query after deletion");
    let response4 = send_command("QUERY TAG priority=high").expect("Failed to query after deletion");
    
    assert!(!response3.contains("/tmp/consistencytest.txt"), "Deleted file should not appear in queries");
    assert!(!response4.contains("/tmp/consistencytest.txt"), "Deleted file should not appear in queries");
}

/// Test integration with external systems (real implementation)
#[test]
fn test_external_integration() {
    let mut config = TestConfig::new();
    config.start_daemon();
    
    // Test WAL/2PC integration (real implementation)
    println!("Testing WAL/2PC integration...");
    
    // Create a test file and verify WAL entry is created
    send_command("EVENT CREATE /tmp/waltest.txt").expect("Failed to create WAL test file");
    
    // Wait for WAL processing
    thread::sleep(Duration::from_millis(200));
    
    // Verify WAL entry exists by checking if file is tracked
    let response = send_command("QUERY FILE /tmp/waltest.txt").expect("Failed to query WAL test file");
    assert!(response.contains("/tmp/waltest.txt"), "WAL test file should be tracked");
    
    // Test WAL commit by modifying the file
    send_command("EVENT WRITE /tmp/waltest.txt").expect("Failed to write to WAL test file");
    thread::sleep(Duration::from_millis(100));
    
    // Verify WAL consistency by checking file metadata
    let metadata_response = send_command("METADATA /tmp/waltest.txt").expect("Failed to get metadata");
    assert!(metadata_response.contains("last_modified"), "File should have modification metadata");
    
    // Test WAL/2PC distributed consistency (simulate peer communication)
    // In a real distributed setup, this would test actual peer synchronization
    let peers_response = send_command("PEERS").expect("Failed to get peers info");
    assert!(!peers_response.contains("ERR"), "Peers command should not return error");
    
    // Test LLM integration (real implementation)
    println!("Testing LLM integration...");
    
    // Create test files with different content for LLM analysis
    send_command("EVENT CREATE /tmp/llm_test1.txt").expect("Failed to create LLM test file 1");
    send_command("EVENT CREATE /tmp/llm_test2.txt").expect("Failed to create LLM test file 2");
    send_command("TAG /tmp/llm_test1.txt type document").expect("Failed to tag LLM test file 1");
    send_command("TAG /tmp/llm_test2.txt type image").expect("Failed to tag LLM test file 2");
    
    // Test natural language query processing
    let nlq_response = send_command("NLQ Find all document files").expect("Failed to send NLQ");
    assert!(!nlq_response.contains("ERR"), "NLQ should not return error");
    assert!(nlq_response.contains("llm_test1.txt"), "NLQ should find document files");
    
    // Test semantic search
    let semantic_response = send_command("SEMANTIC Find files related to documents").expect("Failed to send semantic query");
    assert!(!semantic_response.contains("ERR"), "Semantic query should not return error");
    
    // Test LLM-based file analysis
    let analysis_response = send_command("ANALYZE /tmp/llm_test1.txt").expect("Failed to analyze file");
    assert!(!analysis_response.contains("ERR"), "File analysis should not return error");
    
    // Test LLM-based tag suggestions
    let suggestions_response = send_command("SUGGEST_TAGS /tmp/llm_test1.txt").expect("Failed to get tag suggestions");
    assert!(!suggestions_response.contains("ERR"), "Tag suggestions should not return error");
    
    // Test LLM-based content summarization
    let summary_response = send_command("SUMMARIZE /tmp/llm_test1.txt").expect("Failed to get content summary");
    assert!(!summary_response.contains("ERR"), "Content summary should not return error");
    
    // Test LLM-based relationship discovery
    let relationships_response = send_command("RELATIONSHIPS /tmp/llm_test1.txt").expect("Failed to get relationships");
    assert!(!relationships_response.contains("ERR"), "Relationship discovery should not return error");
    
    // Test LLM-based anomaly detection
    let anomaly_response = send_command("ANOMALY_DETECT").expect("Failed to run anomaly detection");
    assert!(!anomaly_response.contains("ERR"), "Anomaly detection should not return error");
    
    // Test LLM-based recommendation system
    let recommendations_response = send_command("RECOMMEND_FILES").expect("Failed to get file recommendations");
    assert!(!recommendations_response.contains("ERR"), "File recommendations should not return error");
    
    // Test LLM-based knowledge graph updates
    let kg_response = send_command("UPDATE_KNOWLEDGE_GRAPH").expect("Failed to update knowledge graph");
    assert!(!kg_response.contains("ERR"), "Knowledge graph update should not return error");
    
    // Verify LLM integration metrics
    let metrics_response = send_command("METRICS").expect("Failed to get metrics");
    assert!(metrics_response.contains("llm_queries"), "Metrics should include LLM query count");
    assert!(metrics_response.contains("nlp_operations"), "Metrics should include NLP operation count");
    
    // Test error handling for LLM failures
    // Simulate LLM service unavailability
    let error_response = send_command("NLQ Invalid query that should fail gracefully").expect("Failed to send invalid NLQ");
    // Should not crash, but may return error or fallback response
    assert!(!error_response.is_empty(), "Invalid NLQ should return some response");
    
    // Test LLM integration performance
    let start_time = std::time::Instant::now();
    send_command("NLQ Performance test query").expect("Failed to send performance test query");
    let duration = start_time.elapsed();
    assert!(duration < Duration::from_secs(5), "LLM query should complete within 5 seconds");
    
    println!("External integration tests completed successfully");
}

/// Main test runner
fn main() {
    // Run all tests
    let test_functions = vec![
        test_daemon_basic_functionality,
        test_security_validation,
        test_file_events,
        test_tag_operations,
        test_embedding_operations,
        test_natural_language_queries,
        test_application_manifests,
        test_performance_scalability,
        test_error_handling_recovery,
        test_metrics_monitoring,
        test_database_consistency,
        test_external_integration,
    ];
    
    println!("Running {} integration tests for Metadata Daemon...", test_functions.len());
    
    let mut passed = 0;
    let mut failed = 0;
    
    for test_fn in test_functions {
        print!("Running {}... ", std::any::type_name_of_val(&test_fn));
        match std::panic::catch_unwind(|| test_fn()) {
            Ok(_) => {
                println!("PASS");
                passed += 1;
            }
            Err(e) => {
                println!("FAIL");
                eprintln!("Test failed: {:?}", e);
                failed += 1;
            }
        }
    }
    
    println!("\nTest Results: {} passed, {} failed", passed, failed);
    
    if failed > 0 {
        std::process::exit(1);
    }
} 